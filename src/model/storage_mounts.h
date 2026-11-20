/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace storage {

/**
 * De un texto con el formato de `/proc/mounts`, los puntos de montaje que parecen una
 * tarjeta o un pincho (bajo /media, /mnt o /run/media, con un sistema de ficheros de datos).
 * Los espacios vienen escritos como `\040` y se deshacen.
 */
std::vector<std::string> removable_mounts(const std::string& proc_mounts);

} // namespace storage
