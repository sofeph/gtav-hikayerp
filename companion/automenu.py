"""
automenu.py — save menüsü geçişi artık ASI içinde (SaveSkipThread, HIKAYE_WINKEY=1).
Bu modül yalnızca mod_connected event'ini dışa aktarır; server.py bunu kullanır.
"""
import threading

mod_connected = threading.Event()


def run():
    """Artık devre dışı — SaveSkipThread (xpad.cpp) her şeyi halleder."""
    pass
