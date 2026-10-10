import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cc'), 'C compiler required')
class InstallReceipt(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.config = self.root / 'kobalt'
        self.legacy = self.root / '.kobalt.initialized'
        (self.root / 'NickelHook.h').write_text('#define nh_log(...) ((void)0)\n')
        source = self.root / 'check.c'
        source.write_text('''#include "resources.h"
static const struct nh_resource files[] = {{"uninstall", (const unsigned char *)"1\\n", 2}, {0}};
const struct nh_resources nh_resources = {
.config_dir = CONFIG, .uninstall_file = CONFIG "/uninstall-now",
.files = files, .initialized_file = CONFIG "/.initialized"};
int main(int argc, char **argv) { if (argc > 1) nh_resources_uninstalled(); return 0; }
''')
        self.exe = self.root / 'check'
        subprocess.run(['cc', '-I', str(self.root), '-I', str(ROOT / 'toolchains/nickelhook-qt6'),
                        '-DCONFIG="' + str(self.config) + '"', str(source),
                        str(ROOT / 'toolchains/nickelhook-qt6/resources.c'), '-o', str(self.exe)], check=True)

    def run_installer(self):
        subprocess.run([str(self.exe)], check=True)

    def test_first_install_and_folder_removal(self):
        self.config.mkdir()
        self.run_installer()
        self.assertTrue((self.config / '.initialized').is_file())
        self.assertTrue((self.config / 'uninstall').is_file())
        self.assertFalse(self.legacy.exists())
        shutil.rmtree(self.config)
        self.run_installer()
        self.assertFalse(self.config.exists())

    def test_migrate_without_recreating_deleted_uninstall_flag(self):
        self.config.mkdir()
        self.legacy.write_text('1\n')
        self.run_installer()
        self.assertTrue((self.config / '.initialized').is_file())
        self.assertFalse(self.legacy.exists())
        self.assertFalse((self.config / 'uninstall').exists())

    def test_deleted_legacy_folder_is_not_recreated(self):
        self.legacy.write_text('1\n')
        self.run_installer()
        self.assertFalse(self.config.exists())

    def test_uninstall_clears_old_receipt_after_folder_removal(self):
        self.legacy.write_text('1\n')
        subprocess.run([str(self.exe), 'uninstall'], check=True)
        self.assertFalse(self.legacy.exists())
