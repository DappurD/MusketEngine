#ifndef VOXEL_CORE_API_H
#define VOXEL_CORE_API_H

#if defined(_WIN32)
#if defined(VOXEL_CORE_BUILD)
#define VOXEL_CORE_API __declspec(dllexport)
#else
#define VOXEL_CORE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define VOXEL_CORE_API __attribute__((visibility("default")))
#else
#define VOXEL_CORE_API
#endif

#endif // VOXEL_CORE_API_H
