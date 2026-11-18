#include "Core/Scheduler/Scheduler.h"
#include <stdexcept>
#include <string>

using namespace R2MEGA::Core;

struct Target : SchedulerTarget
{
    unsigned vdp = 0, m68k = 0, z80 = 0;
    std::string events;
    void clockVDP() override { ++vdp; events += 'V'; }
    void clockM68K() override { ++m68k; events += 'M'; }
    void clockZ80() override { ++z80; events += 'Z'; }
};

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste do scheduler");
}

int main()
{
    Target target;
    Scheduler scheduler(target);
    scheduler.advance(0);
    require(target.events.empty() && scheduler.masterCycles() == 0);
    scheduler.advance(6);
    require(target.vdp == 6 && target.m68k == 0 && target.z80 == 0);
    scheduler.clock();
    require(target.events.substr(target.events.size() - 2) == "VM");
    scheduler.advance(8);
    require(target.vdp == 15 && target.m68k == 2 && target.z80 == 1);
    scheduler.advance(90);
    require(scheduler.masterCycles() == 105 && target.vdp == 105 && target.m68k == 15 && target.z80 == 7);
    require(target.events.substr(target.events.size() - 3) == "VMZ");
    scheduler.advance(3);
    scheduler.reset();
    target = Target{};
    scheduler.advance(7);
    require(scheduler.masterCycles() == 7 && target.vdp == 7 && target.m68k == 1 && target.z80 == 0);
    scheduler.advance(14);
    require(target.vdp == 21 && target.m68k == 3 && target.z80 == 1);
}
