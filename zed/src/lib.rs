//! Minimal LSP glue: registers fsautocomplete so Zed settings
//! (`lsp.fsautocomplete.binary` / `initialization_options`) can drive it.
//! No auto-download (unlike the marketplace extension) — the binary must be
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
                "fsautocomplete not found on PATH — `dotnet tool install -g fsautocomplete`, \
                 or set lsp.fsautocomplete.binary.path in settings"
                    .to_string()
            })?;

        let args = binary
            .as_ref()
            .and_then(|b| b.arguments.clone())
            .unwrap_or_else(|| vec!["--adaptive-lsp-server-enabled".to_string()]);

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
        // User settings win; the fallback matches the marketplace extension so
        // FSAC loads the workspace without a manual init.
        let user = LspSettings::for_worktree(language_server_id.as_ref(), worktree)
            .ok()
            .and_then(|settings| settings.initialization_options);
        Ok(Some(user.unwrap_or_else(|| {
            zed::serde_json::json!({ "AutomaticWorkspaceInit": true })
        })))
    }
}

zed::register_extension!(FsharpDevExtension);
