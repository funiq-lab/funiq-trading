# Vendor libraries are optional; mock plugins do not require their SDKs.
if(BUILD_VENDOR_ADAPTERS)
    add_subdirectory(ksqmd)
    add_subdirectory(atp)
endif()
