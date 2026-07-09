//! Minimal LSP glue: registers fsautocomplete so Zed settings
//! (`lsp.fsautocomplete.binary` / `initialization_options`) can drive it.
//! No auto-download (unlike the marketplace extension) - the binary must be
//! on PATH or pointed to in settings.

use zed_extension_api::{self as zed, settings::LspSettings, Result};

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
