#include "core/EngineRegistry.h"
#include <string.h>

EngineDescriptor EngineRegistry::_engines[MAX_ENGINES];
size_t EngineRegistry::_engineCount = 0;

bool EngineRegistry::registerEngine(const EngineDescriptor& descriptor) {
    if (_engineCount >= MAX_ENGINES) {
        return false;
    }
    
    // Check for duplicates
    for (size_t i = 0; i < _engineCount; i++) {
        if (strcmp(_engines[i].metadata.id, descriptor.metadata.id) == 0) {
            return false; // Duplicate ID
        }
    }
    
    _engines[_engineCount] = descriptor;
    _engineCount++;
    return true;
}

const EngineDescriptor* EngineRegistry::getDescriptor(const char* id) {
    for (size_t i = 0; i < _engineCount; i++) {
        if (strcmp(_engines[i].metadata.id, id) == 0) {
            return &_engines[i];
        }
    }
    return nullptr;
}

const EngineDescriptor* EngineRegistry::getAllDescriptors(size_t& count) {
    count = _engineCount;
    return _engines;
}

void EngineRegistry::clear() {
    _engineCount = 0;
}
