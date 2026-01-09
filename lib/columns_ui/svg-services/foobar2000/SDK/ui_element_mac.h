#pragma once

#ifdef __APPLE__
// Mac UI element entrypoint
class ui_element_mac : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(ui_element_mac);
public:
    //! @param arg wrapped NSDictionary<NSString*, NSString*>*, see wrapNSObject + unwrapNSObject
    //! @returns wrapped NSViewController, see wrapNSObject + unwrapNSObject
    virtual service_ptr instantiate( service_ptr arg ) = 0;
    //! Tests if this element matches specified name in user's view configuration.
    virtual bool match_name( const char * name ) = 0;
    //! Returns user-readable name. Reserved for future use.
    virtual fb2k::stringRef get_name() = 0;
    //! Returns GUID. Reserved for future use.
    virtual GUID get_guid() = 0;
};
#endif
