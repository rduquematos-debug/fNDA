#ifndef TestDevice_hpp
#define TestDevice_hpp

#include <IOKit/IOService.h>

class TestDevice : public IOService
{
    OSDeclareDefaultStructors(TestDevice);
public:
    virtual bool init(OSDictionary *dict = nullptr) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
};

#endif
