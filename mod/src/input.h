#pragma once
#include <windows.h>

#include <map>

namespace hikaye {

// Menü gezinmesi için tuş okuyucu: basış anında bir kez, basılı tutunca tekrar eder (ok tuşlarıyla değer değiştirme).
// Her tuş için kare başına en fazla bir kez çağrılmalıdır.
class Keys {
public:
    bool Pressed(int vk) {
        State& s = s_[vk];
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = down && !s.down;
        s.down = down;
        if (edge) s.since = GetTickCount64();
        return edge;
    }

    // Basış anında true, 350 ms sonra her 70 ms'de bir tekrar true.
    bool Repeat(int vk) {
        State& s = s_[vk];
        const unsigned long long now = GetTickCount64();
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool fire = false;
        if (down && !s.down) {
            s.since = s.last = now;
            fire = true;
        } else if (down && now - s.since > 350 && now - s.last > 70) {
            s.last = now;
            fire = true;
        }
        s.down = down;
        return fire;
    }

private:
    struct State {
        bool down = false;
        unsigned long long since = 0, last = 0;
    };
    std::map<int, State> s_;
};

}  // namespace hikaye
