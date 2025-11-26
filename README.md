# SharedLibrary
A lightweight wrapper to load dynamic library on Windows/POSIX (Linux/Mac)

# Single File
`SharedLibrary.hpp`

# Simple Usage
```C++

    using namespace sharedlibrary;

    // Creating a library object (lazy loading example)
    auto lib = makeSharedLibrary("./plugins/Srand.dll", true); // Windows, auto binding
    // auto lib = makeSharedLibrary("./plugins/Srand.so", true); // POSIX, auto binding

    // Single Function - Explicit
    auto Get_CPU_Time_I64 = lib->get<unsigned __int64(__cdecl*)(void)>("?Get_CPU_Time_I64@@YA_KXZ");
    std::cout << "Get_CPU_Time_I64() = " << Get_CPU_Time_I64() << '\n';

    // Single Function - Implicit
    unsigned __int64(__cdecl * Get_CPU_Time_I64_1)(void) = nullptr;
    lib->get("?Get_CPU_Time_I64@@YA_KXZ", Get_CPU_Time_I64_1);
    std::cout << "Get_CPU_Time_I64() = " << Get_CPU_Time_I64_1() << '\n';
    
    // Multiple Function 
    double(__cdecl * GetCPUDyFrequency)(void) = nullptr;
    unsigned __int64(__cdecl * Get_CPU_Time_I64_3)(void) = nullptr;

    lib->batchLoad(
        bind("?_GetCPUDyFrequency@@YANXZ", GetCPUDyFrequency),
        bind("?Get_CPU_Time_I64@@YA_KXZ", Get_CPU_Time_I64_3)
    );

    std::cout << "GetCPUDyFrequency() = " << GetCPUDyFrequency() << '\n';
    std::cout << "Get_CPU_Time_I64() = " << Get_CPU_Time_I64_3() << '\n';

    // RAII Offload
    return 0;

```
