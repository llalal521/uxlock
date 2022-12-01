# Format: {A}_{S}
# A = algorithm name, lowercase, without space (must match the src/*.c and src/*.h name)
# S = waiting strategy. original = hardcoded in the algorithm (see README), otherwise spinlock/spin_then_park/park

set(ALGORITHMS
    mcs_spinlock
    mcs_spin_then_park
    mcswake_spin_then_park
    mcssteal_spin_then_park
    aslblock_spin_then_park
    mutexee_original
    utamutexee_original
    malthusian_spin_then_park
    malthusian_spinlock
    spinlock_spin_then_park
    spinlock_spinlock
    spinlock_park
    pthreadinterpose_original
    uxshfl_spinlock
    uta_original
    uxactive_original
    uxpick_spinlock
    cspick_spinlock
    csupperbound_original
    rwtas_original
    utafts_original
    utablocking_original
    utascl_original
    utaspc_original
    utabind_original
)

set(TARGETS_DIR ${CMAKE_CURRENT_SOURCE_DIR})