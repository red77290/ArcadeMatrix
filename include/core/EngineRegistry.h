#pragma once

#include "EngineContract.h"

#define MAX_ENGINES 64

class EngineRegistry {
public:
    // Returns true if successfully registered (enough capacity, no duplicates)
    static bool registerEngine(const EngineDescriptor& descriptor);
    
    // Returns the descriptor for a given ID, or nullptr if not found
    static const EngineDescriptor* getDescriptor(const char* id);
    
    // Returns all registered descriptors
    static const EngineDescriptor* getAllDescriptors(size_t& count);
    
    // Clears the registry (useful for tests)
    static void clear();

private:
    static EngineDescriptor _engines[MAX_ENGINES];
    static size_t _engineCount;
};
