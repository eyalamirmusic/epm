include(CPM)

# Lowercase NAME so a local checkout overrides with -DCPM_eacp_SOURCE=/path/to/eacp.
# epm needs eacp-core only: files, processes, args and JSON. develop, for
# Strings::split/join/toUpper and the string_view Files::writeFile, which main
# does not have yet; epm-lock.json pins the commit.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG develop
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "EACP_BUILD_GRAPHICS OFF"
            "EACP_BUILD_WEBVIEW OFF"
            "EACP_ENABLE_TESTS OFF"
            "EACP_ENABLE_EXAMPLES OFF")
