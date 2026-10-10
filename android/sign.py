#!/usr/bin/env python3
"""Align, sign and verify a release APK with an external PKCS#12 keystore."""
import argparse
import getpass
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def sign_apk(apk, keystore, alias, sdk, password):
    apk = Path(apk).resolve(strict=True)
    keystore = Path(keystore).expanduser().resolve(strict=True)
    tools = Path(sdk) / 'build-tools/36.0.0'
    for tool in ('zipalign', 'apksigner'):
        if not (tools / tool).is_file():
            raise ValueError('Missing Android build tool: ' + str(tools / tool))
    if '\n' in password or '\r' in password:
        raise ValueError('The signing password must fit on one line')
    destination = apk.with_name(apk.stem + '-signed.apk')
    # Keep the previous signed APK intact until both verification steps pass.
    with tempfile.TemporaryDirectory(prefix='.sign-', dir=apk.parent) as temporary:
        aligned = Path(temporary) / 'aligned.apk'
        signed = Path(temporary) / 'signed.apk'
        subprocess.run([str(tools / 'zipalign'), '-P', '16', '4', str(apk), str(aligned)], check=True)
        subprocess.run([str(tools / 'apksigner'), 'sign', '--ks', str(keystore),
                        '--ks-type', 'PKCS12', '--ks-key-alias', alias,
                        '--ks-pass', 'stdin', '--key-pass', 'stdin',
                        '--v4-signing-enabled', 'false', '--out', str(signed), str(aligned)],
                       input=password + '\n' + password + '\n', text=True, check=True)
        subprocess.run([str(tools / 'apksigner'), 'verify', '--verbose', '--print-certs', str(signed)], check=True)
        subprocess.run([str(tools / 'zipalign'), '-c', '-P', '16', '4', str(signed)], check=True)
        signed.replace(destination)
    print('Signed APK: ' + str(destination))
    return destination


def sign_interactively(apk, keystore, alias):
    sdk = os.environ.get('ANDROID_HOME')
    if not sdk:
        raise ValueError('Set ANDROID_HOME to the Android SDK directory')
    if not sys.stdin.isatty():
        raise ValueError('Run signing in your terminal so the passphrase can be entered without echo')
    password = getpass.getpass('PKCS#12 passphrase: ')
    return sign_apk(apk, keystore, alias, sdk, password)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--keystore', required=True, type=Path)
    parser.add_argument('--key-alias', default='kobalt-release')
    args = parser.parse_args()
    try:
        sign_interactively(args.apk, args.keystore, args.key_alias)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
