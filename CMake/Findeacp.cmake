include(CPM)

# Lowercase NAME so a local checkout overrides with -DCPM_eacp_SOURCE=/path/to/eacp.
# epm needs eacp-core only: files, processes, args and JSON.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "EACP_BUILD_GRAPHICS OFF"
            "EACP_BUILD_WEBVIEW OFF"
            "EACP_ENABLE_TESTS OFF"
            "EACP_ENABLE_EXAMPLES OFF")
