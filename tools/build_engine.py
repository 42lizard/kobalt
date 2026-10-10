"""Build Kobalt with the container host's native cross-toolchain architecture."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from functools import lru_cache

from targets import target

TOOLCHAIN6 = target('qt6').toolchain_image


def detect():
    requested = os.environ.get('CONTAINER_ENGINE')
    for name in ([requested] if requested else ['podman', 'docker']):
        if shutil.which(name) and subprocess.run([name, 'info'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            return name
    raise RuntimeError('Install and start Docker or Podman, then run the build again. Set CONTAINER_ENGINE to select one.')


@lru_cache(maxsize=None)
def native_platform(engine):
    # The name does not decide the info format: OrbStack installs a podman
    # command that answers with Docker's structure. Try the likely field first.
    fields = ['{{.Host.Arch}}', '{{.Architecture}}']
    if 'podman' not in Path(engine).name:
        fields.reverse()
    architecture = ''
    for field in fields:
        result = subprocess.run([engine, 'info', '--format', field], capture_output=True, text=True)
        if result.returncode == 0 and result.stdout.strip():
            architecture = result.stdout.strip()
            break
    if not architecture:
        raise RuntimeError(f'Could not read the host architecture from "{engine} info". Set CONTAINER_ENGINE to a working engine.')
    architecture = {'aarch64': 'arm64', 'x86_64': 'amd64'}.get(architecture, architecture)
    if architecture not in ('arm64', 'amd64'):
        raise RuntimeError(f'Unsupported container host architecture: {architecture}')
    return 'linux/' + architecture


def toolchain6_inputs(project):
    # Keep the recipe paths stable so unchanged build steps retain their cache.
    # The context contains only the SDK sources shipped with Kobalt.
    mappings = (('toolchains/qt6/sdk', 'experiments/toolchain-qt6'),
                ('toolchains/qt6/source', 'runtime/qt6-source'))
    return sorted((str(Path(legacy) / path.relative_to(project / folder)), path)
                  for folder, legacy in mappings
                  for path in (project / folder).rglob('*')
                  if path.is_file() and '__pycache__' not in path.parts)


def toolchain6_sources(project):
    return [path for _, path in toolchain6_inputs(project)]


def toolchain6_fingerprint(project):
    digest = hashlib.sha256()
    for name, path in toolchain6_inputs(project):
        digest.update(name.encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def ensure_toolchain6(engine, project, image=TOOLCHAIN6):
    """Reuse the verified image, or build it from the relocated public inputs."""
    with tempfile.TemporaryDirectory(prefix='kobalt-toolchain-') as temporary:
        context = Path(temporary)
        for name, source in toolchain6_inputs(project):
            target = context / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        if ensure_image(engine, image, toolchain6_fingerprint(project),
                        context / 'experiments/toolchain-qt6/Dockerfile', context,
                        notice='Building the firmware 5 toolchain image; its Qt build can take tens of minutes'):
            print(f'Built {image}', flush=True)


def toolchain5_fingerprint(project):
    inputs = sorted((project / 'toolchains/qt5').glob('*'))
    digest = hashlib.sha256()
    for source in inputs:
        digest.update(source.name.encode() + b'\0' + source.read_bytes())
    return digest.hexdigest()


def ensure_toolchain5(engine, project):
    with tempfile.TemporaryDirectory(prefix='kobalt-toolchain5-') as temporary:
        context = Path(temporary)
        shutil.copytree(project / 'toolchains/qt5', context / 'toolchains/qt5')
        ensure_image(engine, target('qt5').toolchain_image, toolchain5_fingerprint(project),
                     context / 'toolchains/qt5/Dockerfile', context)


def ensure_image(engine, image, fingerprint, recipe, context, notice=None):
    """Build an image unless one with this recipe fingerprint exists for the host architecture."""
    result = subprocess.run([engine, 'image', 'inspect', '--format', '{{.Architecture}} {{ index .Config.Labels "kobalt.recipe" }}', image], capture_output=True, text=True)
    if result.returncode == 0 and result.stdout.strip() == native_platform(engine).split('/')[1] + ' ' + fingerprint:
        return False
    if notice:
        print(notice, flush=True)
    subprocess.run([engine, 'build', '--platform', native_platform(engine), '--label', f'kobalt.recipe={fingerprint}', '-t', image, '-f', str(recipe), str(context)], check=True)
    return True


def run(engine, args, **kwargs):
    if engine is None:
        return subprocess.run(args, check=True, **kwargs)
    if args[0] == 'run' and '--platform' not in args:
        args = ['run', '--platform', native_platform(engine), *args[1:]]
    return subprocess.run([engine, *args], check=True, **kwargs)
