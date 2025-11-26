/********************************************************************
* SharedLibrary.hpp
* ---------------------------------------------------------------
* Cross-platform dynamic library wrapper:
* - Supports immediate loading or lazy loading
* - Supports explicit/implicit function type specification
* - Supports one-time batch binding
*
* Dependencies:
* - Windows SDK (>= WinXP SP1)
* - POSIX dlopen (Linux, macOS, BSD, ...)
*
* Compiler Support:
* - C++ 17
* - Enable C++ Exception
* 
* License:
* - MIT License
******************************************************************* */

#pragma once

#include <string>
#include <stdexcept>
#include <utility>
#include <mutex>
#include <functional>
#include <memory>
#include <vector>

// Platform Specific
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Namespace sharedlibrary starts
namespace sharedlibrary
{

    /*--------------------------------------------------------------
     *  Auxiliary structure: Function name; local pointer binding, used for batchLoad()
     *--------------------------------------------------------------*/
    template<class _Func>
    struct _FuncBinding {
        const char* name;   // The ANSI name (or ordinal) of the exported function.
        _Func* ptr;         // Local function pointer variables that need to be populated
    };

    /*--------------------------------------------------------------
     *  SharedLibrary Base Class
     *--------------------------------------------------------------*/
    class SharedLibraryBase {
    public:
        // Constructor
        explicit SharedLibraryBase(std::string_view path, bool delayLoad = false) : libPath_(path), delayLoad_(delayLoad) {}

        // Destructor
        virtual ~SharedLibraryBase() = default;

        // Forbidden Copy
        SharedLibraryBase(const SharedLibraryBase&) = delete;
        SharedLibraryBase& operator=(const SharedLibraryBase&) = delete;

        // Move is okay
        SharedLibraryBase(SharedLibraryBase&&) noexcept = default;
        SharedLibraryBase& operator=(SharedLibraryBase&&) noexcept = default;

        // ---------------------------------------------------------
        //  The public APIs
        // ---------------------------------------------------------

        /** Load immediately (Internal) */
        inline void loadNow() {
            if (isLoaded()) {
                return;               // Already loaded
            }
            nativeLoad();             // Derive Impl
            if (!isLoaded()) {
                throwLastError("loadNow() failed");
                // Failed
            }
        }

        /** Immediate/Delayed Load (if not yet loaded) */
        inline void ensureLoaded(){
            // load Now
            std::call_once(flag_, [this] { this->loadNow(); });
        }

        /** Directly obtain the underlying handle (platform-dependent) */
        virtual inline void* nativeHandle() const noexcept = 0;

        /** Offload (Called by Destructor) */
        inline void unload(){
            if (isLoaded()) {
                nativeUnload();    // Derived Impl
                handle_ = nullptr;
            }
        }

        /** Is already loaded */
        inline bool isLoaded() const noexcept { 
            return handle_ != nullptr; 
        }

        /** Obtaining by explicitly specifying the function type */
        template<class _Func>
        inline _Func get(const char* name){
            ensureLoaded();
            void* p = rawGetSymbol(name);
            if (!p) {
                throwLastError("GetProcAddress", name);
            }
            return reinterpret_cast<_Func>(p);
        }

        /** Obtaining by implicitly derivation function type */
        template<class _Func>
        inline void get(const char* name, _Func& out){
            out = get<_Func>(name);
        }

        /** Obtaining in a batch */
        template<class... Bindings>
        inline void batchLoad(Bindings&&... bindings) {
            (batchLoad_one(std::forward<Bindings>(bindings)), ...);
        }

    protected:
        /** Low-level APIs that derived classes must implement */
        virtual inline void nativeLoad() = 0;    // Load the library into memory successfully, and set handle_ to non-null.
        virtual inline void nativeUnload() = 0;  // Offload
        virtual inline void* rawGetSymbol(const char* name) = 0; // Returns the address of a function or nullptr

        /** Error Handler */
        [[noreturn]] static inline void throwLastError(const char* api, const char* extra = nullptr){
            std::string msg = std::string(api) + " failed";
            if (extra) {
                msg += " – " + std::string(extra);
            }
            throw std::runtime_error(msg);
        }

    protected:
        /** Members */
        std::string libPath_;    // Raw path (UTF-8 / ANSI)
        bool delayLoad_;         // Whether to delay loading
        std::once_flag flag_;    // Flag used for call_once
        void* handle_ = nullptr; // Platform handle (HMODULE / void*)

    private:
         /** The internal implementation of batchLoad */
        template<class _Func>
        inline void batchLoad_one(const _FuncBinding<_Func>& binding) {
            _Func* p = binding.ptr;
            *p = get<_Func>(binding.name);
        }
    };

    /*--------------------------------------------------------------
     *  SharedLibrary Windows Implementation
     *--------------------------------------------------------------*/
#if defined(_WIN32)
    // Windows (NT)
    class SharedLibraryWindows final : public SharedLibraryBase {
    public:
        /** Inherited constructor */
        using SharedLibraryBase::SharedLibraryBase; 

        /** Returns to the original HMODULE */
        void* nativeHandle() const noexcept override {
            return handle_;
        }

    protected:
        /** Native load dll */
        inline void nativeLoad() override
        {
            // Use LoadLibraryExW to allow adding search flags later
            std::wstring wPath = widenPath(libPath_);
            // Here we use the most common default flags;
            // you can also use
            // LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS
            HMODULE h = ::LoadLibraryW(wPath.c_str());
            if (!h) {
                throwLastError("LoadLibraryW", libPath_.c_str());
            }
            handle_ = static_cast<void*>(h);
        }

        /** Native unload dll */
        inline void nativeUnload() override
        {
            if (handle_){
                ::FreeLibrary(static_cast<HMODULE>(handle_));
                handle_ = nullptr;
            }
        }

        /** Native get symbol */
        inline void* rawGetSymbol(const char* name) override {
            if (!handle_) {
                return nullptr;
            }
            FARPROC p = ::GetProcAddress(static_cast<HMODULE>(handle_), name);
            return reinterpret_cast<void*>(p);
        }

    private:
        // Convert UTF-8/ANSI paths to wide characters
        static inline std::wstring widenPath(const std::string& s){
            if (s.empty()) {
                return L"";
            }
            int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w(n, L'\0'); // broadcast
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
            return w;
        }
    };
#else   
    // POSIX (Linux / macOS / BSD)

    /*--------------------------------------------------------------
     *  SharedLibrary POSIX Implementation
     *--------------------------------------------------------------*/
    class SharedLibraryPosix final : public SharedLibraryBase {
    public:
        using SharedLibraryBase::SharedLibraryBase;

        /** Returns POSIX Native Handle */
        void* nativeHandle() const noexcept override {
            return handle_;
        }

    protected:
        /** Native load so */
        inline void nativeLoad() override
        {
            // RTLD_NOW: Resolve Immediately 
            // RTLD_LAZY: Want delayed resolution
            // RTLD_LOCAL: Exported to the global table
            void* h = ::dlopen(libPath_.c_str(), RTLD_LAZY | RTLD_LOCAL);
            if (!h) {
                throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
            }
            handle_ = h;
        }

        /** Native unload so */
        inline void nativeUnload() override {
            if (handle_){
                ::dlclose(handle_);
                handle_ = nullptr;
            }
        }

        /** Native get symbol */
        inline void* rawGetSymbol(const char* name) override
        {
            if (!handle_) return nullptr;
            // Clear lasr error
            ::dlerror();
            void* p = ::dlsym(handle_, name);
            const char* err = ::dlerror();
            if (err) {
                return nullptr;
            }
            return p;
        }
    };

#endif   // _WIN32 / POSIX

    /*--------------------------------------------------------------
     *  SharedLibrary Factory Wrapper
     *--------------------------------------------------------------*/
    inline std::unique_ptr<SharedLibraryBase> makeSharedLibrary(const std::string& path, bool delayLoad = false) {
#if defined(_WIN32)
        // Windows
        return std::make_unique<SharedLibraryWindows>(path, delayLoad);
#else
        return std::make_unique<SharedLibraryPosix>(path, delayLoad);
#endif
    }

    /*--------------------------------------------------------------
     *  SharedLibrary Bind Helper
     *--------------------------------------------------------------*/
    template<class _Func>
    _FuncBinding<_Func> bind(const char* name, _Func& out) {
        return { name, &out };
    }

}
// Namespace sharedlibrary ends
