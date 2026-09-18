-- Minimal, self-contained Neovim config for eyeballing this grammar.
-- Launched via `./build.sh dev nvim` (which builds parser.so first) as:
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

-- Register the freshly-built parsers directly from the repo (no copying).
vim.treesitter.language.add("fsharp", { path = repo .. "/parser.so" })
vim.treesitter.language.add("fsharp_signature", { path = repo .. "/signature/parser.so" })

vim.filetype.add({ extension = { fs = "fsharp", fsx = "fsharp", fsi = "fsharp" } })

-- Load every query from queries/ in place, the Neovim file of a kind winning over
-- the shared Helix one. No nvim-treesitter, so there are no bundled F# queries to
-- fall back to or conflict with.
local kinds = { "highlights", "injections", "locals", "textobjects", "indents", "tags", "folds" }

local function load_queries(lang, nvim_dir, shared_dir)
    for _, name in ipairs(kinds) do
        for _, dir in ipairs({ nvim_dir, shared_dir }) do
            local path = repo .. dir .. name .. ".scm"
            local text = vim.fn.filereadable(path) == 1 and table.concat(vim.fn.readfile(path), "\n") or ""
            if text:match("%S") then -- skip empty stubs
                pcall(vim.treesitter.query.set, lang, name, text)
                break
            end
        end
    end
end

load_queries("fsharp", "/queries/nvim/", "/queries/")
load_queries("fsharp_signature", "/queries/nvim/signature/", "/queries/signature/")

vim.api.nvim_create_autocmd("FileType", {
    pattern = "fsharp",
    callback = function(args)
        -- .fsi files are parsed by the signature grammar.
        local lang = args.file:match("%.fsi$") and "fsharp_signature" or "fsharp"
        vim.treesitter.start(args.buf, lang) -- highlighting
    end,
})

-- Convenience: <leader> is space here; `<space>i` toggles the live parse tree.
vim.g.mapleader = " "
vim.keymap.set("n", "<leader>i", "<cmd>InspectTree<cr>", { desc = "Toggle TS parse tree" })
