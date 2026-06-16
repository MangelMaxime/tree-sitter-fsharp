-- Minimal, self-contained Neovim config for eyeballing this grammar.
-- Launched via `./dev-nvim.sh` (which builds parser.so first) as:
--   nvim -u nvim/init.lua <file>
-- It deliberately loads no plugins: parser + queries come straight from the
-- repo, so what you see is exactly this grammar's output.

-- Repo root = parent of the dir holding this init.lua.
local repo = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h:h")

vim.o.termguicolors = true

-- Theme: catppuccin (frappe), installed via the built-in plugin manager.
-- First launch clones it; afterwards it loads from the local cache.
pcall(vim.pack.add, { { src = "https://github.com/catppuccin/nvim", name = "catppuccin" } })
local ok, cat = pcall(require, "catppuccin")
if ok then
    cat.setup({ flavour = "frappe" })
    pcall(vim.cmd.colorscheme, "catppuccin")
end

-- Register the freshly-built parser directly from the repo (no copying).
vim.treesitter.language.add("fsharp", { path = repo .. "/parser.so" })

vim.filetype.add({ extension = { fs = "fsharp", fsx = "fsharp", fsi = "fsharp" } })

-- Load every query from queries/ in place. No nvim-treesitter, so there are no
-- bundled F# queries to fall back to or conflict with.
for _, name in ipairs({ "highlights", "injections", "locals", "textobjects", "indents", "tags" }) do
    local path = repo .. "/queries/" .. name .. ".scm"
    if vim.fn.filereadable(path) == 1 then
        local text = table.concat(vim.fn.readfile(path), "\n")
        if text:match("%S") then -- skip empty stubs
            pcall(vim.treesitter.query.set, "fsharp", name, text)
        end
    end
end

vim.api.nvim_create_autocmd("FileType", {
    pattern = "fsharp",
    callback = function()
        vim.treesitter.start() -- highlighting
    end,
})

-- Convenience: <leader> is space here; `<space>i` toggles the live parse tree.
vim.g.mapleader = " "
vim.keymap.set("n", "<leader>i", "<cmd>InspectTree<cr>", { desc = "Toggle TS parse tree" })
