add_custom_target(pocketbook-package
    COMMAND bash "${KOBALT_ROOT}/platforms/pocketbook/package.sh" "$<TARGET_FILE:Kobalt>" "${KOBALT_ASSETS}" "${KOBALT_ROOT}/build/pocketbook/Kobalt-PB743K3.zip"
    DEPENDS Kobalt VERBATIM)
