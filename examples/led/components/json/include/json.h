#pragma once

/*
 * Compatibility shim component for ESP-IDF component resolution.
 *
 * Some third-party components still declare a dependency on a component
 * named `json`. Newer ESP-IDF releases do not provide that component name
 * in the registry. The HomeKit stack used by this example ships its own
 * JSON implementation internally, so this shim only satisfies dependency
 * resolution and does not provide APIs.
 */
