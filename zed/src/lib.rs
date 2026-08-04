//! Minimal LSP + DAP glue: registers fsautocomplete so Zed settings
//! (`lsp.fsautocomplete.binary` / `initialization_options`) can drive it, and
//! netcoredbg as the debug adapter. No auto-download (unlike the marketplace
//! extension) - both binaries must be on PATH or pointed to in settings.

use zed_extension_api::{self as zed, settings::LspSettings, Result};

const NETCOREDBG: &str = "netcoredbg";

struct FsharpDevExtension;

impl zed::Extension for FsharpDevExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let binary = LspSettings::for_worktree(language_server_id.as_ref(), worktree)
            .ok()
            .and_then(|settings| settings.binary);

        let path = binary
            .as_ref()
            .and_then(|b| b.path.clone())
            .or_else(|| worktree.which("fsautocomplete"))
            .ok_or_else(|| {
                "fsautocomplete not found on PATH - `dotnet tool install -g fsautocomplete`, \
                 or set lsp.fsautocomplete.binary.path in settings"
                    .to_string()
            })?;

        // Default args merged with the user's binary.arguments (deduped, so a
        // user repeating a default flag doesn't pass it twice).
        let mut args = vec!["--adaptive-lsp-server-enabled".to_string()];
        if let Some(user_args) = binary.as_ref().and_then(|b| b.arguments.clone()) {
            for arg in user_args {
                if !args.contains(&arg) {
                    args.push(arg);
                }
            }
        }

        let mut env = worktree.shell_env();
        if let Some(extra) = binary.and_then(|b| b.env) {
            env.extend(extra);
        }

        Ok(zed::Command {
            command: path,
            args,
            env,
        })
    }

    fn language_server_initialization_options(
        &mut self,
        language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<Option<zed::serde_json::Value>> {
        // Extension defaults, deep-merged with the user's
        // lsp.fsautocomplete.initialization_options - user keys win per key,
        // so setting one option doesn't wipe the defaults.
        let mut options = zed::serde_json::json!({ "AutomaticWorkspaceInit": true });
        if let Some(user) = LspSettings::for_worktree(language_server_id.as_ref(), worktree)
            .ok()
            .and_then(|settings| settings.initialization_options)
        {
            merge(&mut options, user);
        }
        Ok(Some(options))
    }

    // --- Debugger (netcoredbg) ---

    fn get_dap_binary(
        &mut self,
        adapter_name: String,
        config: zed::DebugTaskDefinition,
        user_provided_debug_adapter_path: Option<String>,
        worktree: &zed::Worktree,
    ) -> Result<zed::DebugAdapterBinary> {
        if adapter_name != NETCOREDBG {
            return Err(format!("unsupported debug adapter '{adapter_name}'"));
        }

        let command = user_provided_debug_adapter_path
            .or_else(|| worktree.which(NETCOREDBG))
            .ok_or_else(|| {
                "netcoredbg not found on PATH - install it \
                 (https://github.com/Samsung/netcoredbg) or set \
                 dap.netcoredbg.binary.path in settings"
                    .to_string()
            })?;

        let request = self.dap_request_kind(
            adapter_name,
            zed::serde_json::from_str(&config.config)
                .map_err(|error| format!("invalid debug config: {error}"))?,
        )?;

        Ok(zed::DebugAdapterBinary {
            command: Some(command),
            arguments: vec!["--interpreter=vscode".to_string()],
            envs: worktree.shell_env(),
            cwd: Some(worktree.root_path()),
            connection: config
                .tcp_connection
                .map(zed::resolve_tcp_template)
                .transpose()?,
            request_args: zed::StartDebuggingRequestArguments {
                configuration: config.config,
                request,
            },
        })
    }

    fn dap_request_kind(
        &mut self,
        _adapter_name: String,
        config: zed::serde_json::Value,
    ) -> Result<zed::StartDebuggingRequestArgumentsRequest> {
        let request = config
            .get("request")
            .and_then(|request| request.as_str())
            .ok_or("debug config needs a 'request' field ('launch' or 'attach')")?;
        match request {
            "launch" => Ok(zed::StartDebuggingRequestArgumentsRequest::Launch),
            "attach" => Ok(zed::StartDebuggingRequestArgumentsRequest::Attach),
            other => Err(format!(
                "unsupported request '{other}' (expected 'launch' or 'attach')"
            )),
        }
    }

    fn dap_config_to_scenario(&mut self, config: zed::DebugConfig) -> Result<zed::DebugScenario> {
        use zed::serde_json::{json, Map, Value};

        let adapter_config = match config.request {
            zed::DebugRequest::Launch(launch) => {
                let mut map = Map::new();
                map.insert("request".into(), json!("launch"));
                map.insert("program".into(), json!(launch.program));
                if !launch.args.is_empty() {
                    map.insert("args".into(), json!(launch.args));
                }
                if let Some(cwd) = launch.cwd {
                    map.insert("cwd".into(), json!(cwd));
                }
                if !launch.envs.is_empty() {
                    let envs: Map<String, Value> = launch
                        .envs
                        .into_iter()
                        .map(|(key, value)| (key, json!(value)))
                        .collect();
                    map.insert("env".into(), Value::Object(envs));
                }
                if let Some(stop) = config.stop_on_entry {
                    map.insert("stopAtEntry".into(), json!(stop));
                }
                Value::Object(map)
            }
            zed::DebugRequest::Attach(attach) => {
                let process_id = attach
                    .process_id
                    .ok_or("attaching requires a process id")?;
                json!({ "request": "attach", "processId": process_id })
            }
        };

        Ok(zed::DebugScenario {
            label: config.label,
            adapter: config.adapter,
            build: None,
            config: adapter_config.to_string(),
            tcp_connection: None,
        })
    }
}

/// Recursively overlay `overlay` onto `base`; objects merge per key,
/// everything else is replaced by the overlay value.
fn merge(base: &mut zed::serde_json::Value, overlay: zed::serde_json::Value) {
    use zed::serde_json::Value;
    match (base, overlay) {
        (Value::Object(base_map), Value::Object(overlay_map)) => {
            for (key, value) in overlay_map {
                merge(base_map.entry(key).or_insert(Value::Null), value);
            }
        }
        (base_slot, overlay) => *base_slot = overlay,
    }
}

zed::register_extension!(FsharpDevExtension);
