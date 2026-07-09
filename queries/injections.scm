; Keep the shared rules in sync with zed/injections.scm (same syntax; it only
; adds Zed-specific extras).

((block_doc_comment) @injection.content
 (#set! injection.language "markdown"))

((xml_doc_comment) @injection.content
 (#set! injection.language "xml")
 (#set! injection.combined))
