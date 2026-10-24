#!/bin/bash
# Run in the prepared Android SDK. Passwords arrive only through the environment.
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: sign-apk.sh UNSIGNED_APK OUTPUT_APK' >&2; exit 2; }
for name in KOBALT_ANDROID_KEYSTORE KOBALT_ANDROID_KEY_ALIAS KOBALT_ANDROID_KEYSTORE_PASSWORD KOBALT_ANDROID_KEY_PASSWORD KOBALT_ANDROID_CERT_SHA256; do
    [[ -n ${!name:-} ]] || { echo "Missing $name" >&2; exit 2; }
done
[[ -f "$KOBALT_ANDROID_KEYSTORE" ]] || { echo 'Release keystore is missing' >&2; exit 2; }
[[ "$KOBALT_ANDROID_CERT_SHA256" =~ ^[0-9a-fA-F]{64}$ ]] || { echo 'Expected a 64-character certificate SHA-256' >&2; exit 2; }
signer="${ANDROID_HOME:?}/build-tools/${KOBUILD_ANDROID_BUILD_TOOLS:?}/apksigner"
aligner="${ANDROID_HOME}/build-tools/${KOBUILD_ANDROID_BUILD_TOOLS}/zipalign"
[[ -x $signer && -x $aligner ]] || { echo 'Prepared Android signing tools are missing' >&2; exit 2; }
stage=$(mktemp -d "${2%/*}/apk-signing.XXXXXX")
trap 'rm -rf "$stage"' EXIT
"$aligner" -f -P 16 4 "$1" "$stage/aligned.apk"
"$signer" sign --ks "$KOBALT_ANDROID_KEYSTORE" --ks-key-alias "$KOBALT_ANDROID_KEY_ALIAS" \
    --ks-pass env:KOBALT_ANDROID_KEYSTORE_PASSWORD --key-pass env:KOBALT_ANDROID_KEY_PASSWORD \
    --out "$stage/signed.apk" "$stage/aligned.apk"
"$signer" verify --verbose --print-certs "$stage/signed.apk" > "$stage/verification.txt"
actual=$(sed -n 's/^Signer #1 certificate SHA-256 digest: //p' "$stage/verification.txt" | tr '[:upper:]' '[:lower:]')
expected=$(printf '%s' "$KOBALT_ANDROID_CERT_SHA256" | tr '[:upper:]' '[:lower:]')
[[ "$actual" == "$expected" ]] || { echo 'APK signing certificate does not match the expected release key' >&2; exit 1; }
"$aligner" -c -P 16 4 "$stage/signed.apk"
mv "$stage/signed.apk" "$2"
cp "$stage/verification.txt" "$2.signing.txt"
echo "Verified release APK certificate: $actual"
