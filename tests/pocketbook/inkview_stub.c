/* Headless ARM test only: display, task and power calls have no hardware effects. */
void BanSleep(int seconds) {}
void iv_sleepmode(int enabled) {}
int GetBatteryPower(void) {
    return 90;
}
int IsCharging(void) {
    return 0;
}
void FullUpdate(void) {}
int GetCurrentTask(void) {
    return 1;
}
int SetSubtaskInfo(int task, int subtask, const char *name, const char *book) {
    return 0;
}
void BookReady(const char *path) {}
void AddLastOpen(const char *path) {}
void SetOrientation(int orientation) {}
int GetOrientation(void) {
    return 0;
}
int ScreenWidth(void) {
    return 1404;
}
int ScreenHeight(void) {
    return 1872;
}
int PanelHeight(void) {
    return 137;
}
int OpenTask(const char *path, int argc, const char *const *argv, int flags) {
    return -1;
}
