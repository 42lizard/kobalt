"""Exclusive locks for Kobalt build outputs."""
import fcntl


def acquire(path, wait=False):
    lock = path.open("a+")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | (0 if wait else fcntl.LOCK_NB))
    except BlockingIOError:
        lock.close()
        raise SystemExit(f"Already in use: {path.stem}. Nothing was changed.")
    return lock
