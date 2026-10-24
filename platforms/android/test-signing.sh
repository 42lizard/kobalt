#!/bin/bash
# Exercise the release signer with a disposable key and a real unsigned APK.
set -euo pipefail
[[ $# == 1 && -f $1 ]] || { echo 'Expected an unsigned APK' >&2; exit 2; }
root=$(cd "$(dirname "$0")/../.." && pwd)
stage=$(mktemp -d "$root/build/signing-check.XXXXXX")
trap 'rm -rf "$stage"' EXIT
export KOBALT_ANDROID_KEYSTORE="$stage/disposable.keystore"
export KOBALT_ANDROID_KEY_ALIAS=validation
export KOBALT_ANDROID_KEYSTORE_PASSWORD=disposable-test-password
export KOBALT_ANDROID_KEY_PASSWORD=disposable-test-password
keytool -genkeypair -keystore "$KOBALT_ANDROID_KEYSTORE" -storepass "$KOBALT_ANDROID_KEYSTORE_PASSWORD" \
    -keypass "$KOBALT_ANDROID_KEY_PASSWORD" -alias validation -keyalg RSA -keysize 2048 \
    -validity 2 -dname 'CN=Disposable signing regression' > "$stage/key.log" 2>&1
keytool -exportcert -keystore "$KOBALT_ANDROID_KEYSTORE" -storepass "$KOBALT_ANDROID_KEYSTORE_PASSWORD" \
    -alias validation > "$stage/certificate.der"
KOBALT_ANDROID_CERT_SHA256=$(sha256sum "$stage/certificate.der" | awk '{print $1}')
export KOBALT_ANDROID_CERT_SHA256
bash "$root/platforms/android/sign-apk.sh" "$1" "$stage/valid.apk" > "$stage/signing.log"
[[ -s "$stage/valid.apk" && -s "$stage/valid.apk.signing.txt" ]]
export KOBALT_ANDROID_CERT_SHA256=0000000000000000000000000000000000000000000000000000000000000000
if bash "$root/platforms/android/sign-apk.sh" "$1" "$stage/invalid.apk" > "$stage/rejected.log" 2>&1; then
    echo 'Signing accepted the wrong certificate' >&2; exit 1
fi
grep -q 'does not match' "$stage/rejected.log"
[[ ! -e "$stage/invalid.apk" ]]
echo 'PASS: release signing, alignment and wrong-certificate rejection'
