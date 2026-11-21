#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include "../resource_ids.h"

namespace simulator {

struct EmbeddedData {
    const uint8_t* data;
    size_t size;
};

inline EmbeddedData getEmbeddedResource(int id) {
    HMODULE module = GetModuleHandleW(nullptr);

    HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(id), RT_RCDATA
    );

    if (!resource)
        throw std::runtime_error(
            "Recurso interno nao encontrado: " + std::to_string(id)
        );

    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded)
        throw std::runtime_error("Falha ao carregar recurso interno.");

    const auto* data = static_cast<const uint8_t*>(LockResource(loaded));
    DWORD size = SizeofResource(module, resource);

    if (!data || !size)
        throw std::runtime_error("Recurso interno vazio ou invalido.");

    return { data, static_cast<size_t>(size) };
}

}
