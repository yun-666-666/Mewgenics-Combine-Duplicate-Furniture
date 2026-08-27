#include "prompt_native.h"
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include "mew_ui_api.h"

#include <string.h>

/* MewUI's documented native ABI, without its timer or per-frame button scan. */
static UINT_PTR g_base;
static volatile LONG g_blocked;
static MewFnButtonCanActivate g_next_can_activate;

static uint8_t __fastcall CanActivate(void* button, int32_t index, uint8_t strict) {
    if (InterlockedCompareExchange(&g_blocked, 0, 0)) return 0;
    return g_next_can_activate ? g_next_can_activate(button, index, strict) : 0;
}

int cdf_prompt_init(void) {
    MewjectorAPI api;
    if (!MJ_Resolve(&api)) return 0;
    g_base = api.GetGameBase();
    return g_base && api.InstallHook(
        MEW_RVA_BUTTON_CAN_ACTIVATE, MEW_RVA_BUTTON_CAN_ACTIVATE_STOLEN_BYTES,
        (void*)CanActivate, (void**)&g_next_can_activate, 45, "CombineDuplicateFurniture");
}

void cdf_prompt_block_input(int blocked) {
    InterlockedExchange(&g_blocked, blocked ? 1 : 0);
}

static int Readable(const void* pointer, size_t size) {
    MEMORY_BASIC_INFORMATION info;
    const uintptr_t address = (uintptr_t)pointer;
    return pointer && VirtualQuery(pointer, &info, sizeof(info)) == sizeof(info) &&
        info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        size <= (uintptr_t)info.BaseAddress + info.RegionSize - address;
}

static int Executable(const void* pointer) {
    MEMORY_BASIC_INFORMATION info;
    DWORD access;
    if (!pointer || !VirtualQuery(pointer, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    access = info.Protect & 0xFF;
    return access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
        access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
}

void* cdf_prompt_house(void) {
    __try {
        MewDirector* director = *(MewDirector**)(g_base + MEW_RVA_MEWDIRECTOR_SINGLETON);
        void** current;
        if (!director || !director->director) return NULL;
        for (current = director->director->scenes.begin;
             current && current < director->director->scenes.end; ++current) {
            uint8_t* scene = (uint8_t*)*current;
            MewNarrowString* name;
            const char* value;
            if (!scene) continue;
            name = (MewNarrowString*)(scene + MEW_OFF_SCENE_NAME);
            value = name->capacity > 15 ? name->storage.heap_ptr : name->storage.inline_buf;
            if (name->size == 5 && memcmp(value, "House", 5) == 0 &&
                !scene[MEW_OFF_SCENE_DOING_DESTRUCTION] &&
                !scene[MEW_OFF_SCENE_SKIP_READY_TICK_A] &&
                !scene[MEW_OFF_SCENE_SKIP_READY_TICK_B] &&
                !scene[MEW_OFF_SCENE_SKIP_READY_TICK_C]) return scene;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return NULL;
}

void* cdf_prompt_child(void* root, const char* name) {
    MewNarrowString key = {0};
    size_t length = strlen(name);
    if (!root || length > 15) return NULL;
    memcpy(key.storage.inline_buf, name, length);
    key.size = length;
    key.capacity = 15;
    __try {
        return ((MewFnFindChildByName)(g_base + MEW_RVA_UI_FIND_CHILD_BY_NAME))(root, &key);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

void* cdf_prompt_find(void* scene, void** owner) {
    *owner = NULL;
    __try {
        MewPodVectorPtr* components = *(MewPodVectorPtr**)((uint8_t*)scene + MEW_OFF_SCENE_COMPONENT_LISTS);
        uint32_t index;
        void* previous = NULL;
        if (!Readable(components, sizeof(*components)) || components->size > components->capacity ||
            !Readable(components->data, (size_t)components->size * sizeof(void*))) return NULL;
        for (index = 0; index < components->size; ++index) {
            void* component = components->data[index];
            void* root;
            void* children;
            void** vtable;
            void* prompt;
            if (!Readable(component, 0x40)) continue;
            root = *(void**)((uint8_t*)component + 0x38);
            if (root == component || root == previous || !Readable(root, 0x88)) continue;
            previous = root;
            children = *(void**)((uint8_t*)root + 0x80);
            /* +0x38 is not always a UI root. Validate its child collection before native lookup. */
            if (!Readable(children, sizeof(void*))) continue;
            vtable = *(void***)children;
            if (!Readable(vtable, 4 * sizeof(void*)) || !Executable(vtable[0]) || !Executable(vtable[3])) continue;
            prompt = cdf_prompt_child(root, "cdf_prompt");
            if (prompt) {
                *owner = component;
                return prompt;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return NULL;
}

int cdf_prompt_text(void* node, const char* text) {
    MewNarrowString value = {0};
    int initialized = 0, transferred = 0;
    if (!node) return 0;
    __try {
        ((MewFnInitNarrowString)(g_base + MEW_RVA_INIT_NARROW_STRING))(&value, text);
        initialized = 1;
        /* The engine consumes this string; never free it again after the call. */
        transferred = 1;
        ((MewFnSetTextElementString)(g_base + MEW_RVA_SET_TEXT_ELEMENT_STRING))(node, &value, 0, 1);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (initialized && !transferred)
            ((MewFnDestroyNarrowString)(g_base + MEW_RVA_DESTROY_NARROW_STRING))(&value);
        return 0;
    }
}

int cdf_prompt_frame(void* node, int frame) {
    if (!node) return 0;
    __try {
        ((MewFnMovieClipGotoAndPlayFrame)(g_base + MEW_RVA_MOVIECLIP_GOTO_AND_PLAY_FRAME))(node, frame);
        *((uint8_t*)node + 0x09) &= (uint8_t)~0x02;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
