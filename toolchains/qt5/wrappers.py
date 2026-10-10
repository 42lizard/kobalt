"""Use the old runtime headers and libraries with the modern ARM compiler."""
from pathlib import Path

loader = Path('/tc/qt5-host/lib64/ld-linux-x86-64.so.2')
loader.unlink()
loader.symlink_to('../lib/x86_64-linux-gnu/ld-2.28.so')
root = Path('/tc/qt5')
bin = root / 'bin'
bin.mkdir(exist_ok=True)
for name in ('gcc', 'g++'):
    includes = '' if name == 'gcc' else ' -fno-sized-deallocation -include /tc/qt5/cxx-compat.h -nostdinc++ -isystem /tc/qt5/include/c++/4.9.4 -isystem /tc/qt5/include/c++/4.9.4/arm-nickel-linux-gnueabihf'
    output = bin / name
    output.write_text('#!/bin/sh\nexec /usr/bin/arm-linux-gnueabihf-' + name + '-11'
                      + ' --sysroot=/tc/qt5/sysroot -B/tc/qt5/sysroot/usr/lib/ -L/tc/qt5/sysroot/lib -L/tc/qt5/sysroot/usr/lib -no-pie'
                      + ' -march=armv7-a -mfpu=neon -mfloat-abi=hard' + includes + ' "$@"\n')
    output.chmod(0o755)
for name in ('moc', 'rcc', 'uic'):
    tool = root / 'sysroot/usr/bin' / name
    tool.rename(tool.with_suffix('.x86_64'))
    tool.write_text('#!/bin/sh\nexec qemu-x86_64 -L /tc/qt5-host ' + str(tool.with_suffix('.x86_64')) + ' "$@"\n')
    tool.chmod(0o755)
for metadata in (root / 'sysroot/usr/lib/pkgconfig').glob('*.pc'):
    metadata.write_text(metadata.read_text().replace('prefix=/usr\n', 'prefix=/tc/qt5/sysroot/usr\n'))
# NickelTC's Qt5Gui has QT_NO_OPENGL and no GL dependency. Its old generated
# CMake metadata still asks for desktop GL; qmake correctly omits it.
extras = root / 'sysroot/usr/lib/cmake/Qt5Gui/Qt5GuiConfigExtras.cmake'
extras.write_text(extras.read_text().replace('_qt5gui_find_extra_libs(OPENGL "GL" "" "")', '')
                  .replace('set(Qt5Gui_OPENGL_IMPLEMENTATION GL)', 'set(Qt5Gui_OPENGL_IMPLEMENTATION "")'))
