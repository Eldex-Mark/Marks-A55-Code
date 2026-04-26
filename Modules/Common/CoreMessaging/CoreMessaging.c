
#include <stdlib.h> //for malloc(), etc.
#include <stdio.h> //for printf(), etc.
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h> //for memcpy(), etc.

#include "CoreMessaging.h"
#if ( defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) && SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 )
 #include "CoreM33_CoreMessaging.h"
#elif ( defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) && SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 )
 #include "CoreA55_CoreMessaging.h"
#endif

#include "platform_info.h"

static CoreMessaging_EndPointDescriptor CoreMessaging_EndPoints [COREMESSAGING_ENDPOINT_AMOUNT];

static CoreMessaging_EventDescriptor CoreMessaging_Events [COREMESSAGING_ENDPOINT_AMOUNT] [COREMESSAGING_EVENT_AMOUNT_MAX];

static uint8_t CoreMessaging_Incoming_Message [COREMESSAGING_MESSAGE_SIZE_MAX];
static uint8_t CoreMessaging_Outgoing_Message [COREMESSAGING_MESSAGE_SIZE_MAX];

static int CoreMessaging_Incoming_ByteCount = 0;
static int CoreMessaging_Outgoing_ByteCount = 0;

static size_t CoreMessaging_elemSize(CoreMessaging_VariableTypes type);

static size_t CoreMessaging_castValueToVariable(CoreMessaging_VariableTypes type,
                                  CoreMessaging_ValueContainer value,
                                  void *variable_pointer);
                                  
//static CoreMessaging_ValueContainer CoreMessaging_castVariableToValue (CoreMessaging_VariableTypes type, void* variable_pointer);

static inline CoreMessaging_ValueContainer CoreMessaging_castVariableToValue(CoreMessaging_VariableTypes type, const void *p);                                                        

static inline size_t CoreMessaging_elemSize(CoreMessaging_VariableTypes type)
{
    CoreMessaging_ValueContainer dummy;
    memset(&dummy, 0, sizeof(dummy));
    return CoreMessaging_castValueToVariable(type, dummy, NULL);
}

static void CoreMsg_logWrite(uint32_t ep,
                             uint8_t var_index,
                             const CoreMessaging_VariableDescriptor *vd,
                             size_t offset,
                             const uint8_t *dst,
                             size_t nbytes)
{
    LPRINTF("[CoreMsg WRITE] ep=%u var=%u type=%u count=%u dst=%p off=%zu nbytes=%zu",
            ep, var_index, (unsigned)vd->Type, (unsigned)vd->Count,
            (void*)vd->VariablePointer, offset, nbytes);

    // Optional: dump up to first 32 bytes so logs don't explode
    size_t dump_n = (nbytes > 32) ? 32 : nbytes;
    LPRINTF(" data:");
    for (size_t i = 0; i < dump_n; i++) {
        LPRINTF(" %02X", dst[i]);
    }
    if (dump_n < nbytes) LPRINTF(" ...");
    LPRINTF("\n");
    fflush(stdout);
}

int CoreMessaging_processIncoming(const uint8_t *data, size_t len, uint32_t src_endpoint_index)
{
    LPRINTF("[CoreMsg IN] data=%p len=%zu src_ep=%u\n",
           (const void *)data,
           len,
           src_endpoint_index);
    LPRINTF("[CoreMsg IN] first 8 bytes:");
    for (int i = 0; i < 8 && i < len; i++)
    LPRINTF(" %02X", data[i]);
    LPRINTF("\n");
    fflush(stdout);
    if (!data || len == 0) return -1;
    if (src_endpoint_index >= COREMESSAGING_ENDPOINT_AMOUNT) return -2;

    CoreMessaging_VariableDescriptor *vars = CoreMessaging_EndPoints[src_endpoint_index].Variables;
    CoreMessaging_EventDescriptor    *evts = CoreMessaging_EndPoints[src_endpoint_index].Events;
    LPRINTF("[EP DBG] src_ep=%u EndPoints=%p\n",
        src_endpoint_index,
        (void *)CoreMessaging_EndPoints);

    LPRINTF("[EP DBG] vars=%p evts=%p\n",
        (void *)vars,
        (void *)evts);
    fflush(stdout);
    
    if (!vars || !evts) return -3;

    const uint8_t *p   = data;
    const uint8_t *end = data + len;
    LPRINTF("BEFORE:while()\n");
    fflush(stdout);
    while (p < end) {
        uint8_t cmd = *p++;

        if (cmd == COREMESSAGING_COMMAND__SET_VARIABLE) {
            if (end - p < 1) return -10; // missing variable index
            uint8_t var_index = *p++;

            // bounds check: walk until END marker
            int i = 0;
            for (; vars[i].Type != COREMESSAGING_VARIABLE_TYPE__END; ++i) {
                if (i == var_index) break;
            }
            if (vars[i].Type == COREMESSAGING_VARIABLE_TYPE__END) {
                // unknown index, cannot know payload size -> stop safely
                return -11;
            }

            CoreMessaging_VariableDescriptor *vd = &vars[var_index];
            size_t elem_sz = CoreMessaging_elemSize(vd->Type);
            if (elem_sz == 0) return -12;

            if (vd->Count <= 1) {
                // scalar: read exactly elem_sz bytes
                if ((size_t)(end - p) < elem_sz) return -13;

                CoreMessaging_ValueContainer v;
                memset(&v, 0, sizeof(v));

                // Copy raw bytes into ValueContainer in a way compatible with your union usage.
                // Since castValueToVariable() writes from ValueContainer into memory,
                // easiest is: memcpy directly into destination, then refresh PreviousValue from variable.
                if (vd->VariablePointer) {
                    LPRINTF("[CoreMsg memcpy] dst=%p src=%p size=%zu\n",
                    vd->VariablePointer, p, elem_sz);

                uint8_t *dst = (uint8_t*)vd->VariablePointer;
                const uint8_t *src = (const uint8_t*)p;

                for (size_t i = 0; i < elem_sz; i++) {
                     dst[i] = src[i];
                     LPRINTF("dst[%d]=%x src[%d]=%x\n",i,dst,i,src);
                     fflush(stdout);
                }
                    
 //                   memcpy(vd->VariablePointer, p, elem_sz);                 
                    CoreMsg_logWrite(src_endpoint_index, var_index, vd,
                        0,
                        (const uint8_t*)vd->VariablePointer,
                        elem_sz);                  
                }
                p += elem_sz;

                // Update PreviousValue to match new variable value (prevents "false change")
                v = CoreMessaging_castVariableToValue(vd->Type, vd->VariablePointer);
                vd->PreviousValue = v;
            }
            else {
                // array: your sender may transmit full array OR only the changed tail.
                size_t array_sz = (size_t)vd->Count * elem_sz;

                // Remaining bytes until next command are ambiguous in this protocol.
                // In practice, your sender emits: cmd+index then raw bytes and then next cmd.
                // Because there's no delimiter, we can only safely parse arrays if:
                //   - the rest of the frame is ONLY this array, OR
                //   - the sender always sends full array size.
                //
                // Best-effort strategy:
                //   If remaining >= array_sz -> take exactly array_sz as full array.
                //   Else -> treat remaining bytes as "tail update" aligned to end of array.
                size_t rem = (size_t)(end - p);
                size_t ncopy = (rem >= array_sz) ? array_sz : rem;

                if (vd->VariablePointer && ncopy > 0) {
                    size_t start = 0;
                    if (ncopy < array_sz) {
                        // infer tail start (matches your CoreMessaging_sendArray() behavior)
                        start = array_sz - ncopy;
                    }
                    memcpy(((uint8_t*)vd->VariablePointer) + start, p, ncopy);
                }

                // Advance
                p += ncopy;

                // Refresh PreviousValue buffer so change detection doesn't re-fire
                if (vd->PreviousValue.Pointer && vd->VariablePointer) {
                    // ensure PreviousValue.Pointer exists (allocated in CoreMessaging_init)
                    memcpy(vd->PreviousValue.Pointer, vd->VariablePointer, array_sz);
                }

                // NOTE: because there is no array-length delimiter, if sender appends another
                // command after a partial array, we can't detect it. The safest fix is to
                // include length or start_index in the wire format for arrays.
            }
        }
        else if (cmd == COREMESSAGING_COMMAND__SEND_EVENT) {
            if (end - p < 1) return -20; // missing event id
            uint8_t event_id = *p++;

            if (event_id < COREMESSAGING_EVENT_AMOUNT) {
                // mark triggered; your refresh() will call CoreMessaging_callEvent()
                evts[event_id].Triggered = true;

                // Optionally call immediately:
                // if (evts[event_id].callbackFunction) evts[event_id].callbackFunction();
            }
        }
        else {
            // Unknown command -> stop to avoid desync
            return -30;
        }
    }

    return 0;
}

const uint8_t *CoreMessaging_getOutgoingMessage(size_t *len)
{
    if (len) *len = (size_t)CoreMessaging_Outgoing_ByteCount;
    return CoreMessaging_Outgoing_Message;
}

__attribute__((always_inline))
static inline CoreMessaging_VariableTypes CoreMessaging_getTypeClass (CoreMessaging_VariableTypes type) {
    if ( /*COREMESSAGING_VARIABLE_TYPE__INT__START < type &&*/ type < COREMESSAGING_VARIABLE_TYPE__INT__END) {
        return COREMESSAGING_VARIABLE_TYPE_CATEGORY__INT;
    }
    else { //if ( COREMESSAGING_VARIABLE_TYPE__FLOAT__START < type && type < COREMESSAGING_VARIABLE_TYPE__FLOAT__END) {
        return COREMESSAGING_VARIABLE_TYPE_CATEGORY__FLOAT;
    }
}


/*__attribute__((always_inline))
static inline CoreMessaging_ValueContainer CoreMessaging_castVariableToValue (CoreMessaging_VariableTypes type, void* variable_pointer) {
    static CoreMessaging_ValueContainer ValueContainer;
    if ( CoreMessaging_getTypeClass( type ) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__INT ) {
        switch (type) { //might take advantage of computed goto for faster operation
            case COREMESSAGING_VARIABLE_TYPE__BOOL:    ValueContainer.Int =  * (bool*)     variable_pointer;  break;
            case COREMESSAGING_VARIABLE_TYPE__INT8:    ValueContainer.Int =  * (int8_t*)   variable_pointer;  break;
            case COREMESSAGING_VARIABLE_TYPE__UINT8:   ValueContainer.Int =  * (uint8_t*)  variable_pointer;  break;
            case COREMESSAGING_VARIABLE_TYPE__INT16:   ValueContainer.Int =  * (int16_t*)  variable_pointer;  break;
            case COREMESSAGING_VARIABLE_TYPE__UINT16:  ValueContainer.Int =  * (uint16_t*) variable_pointer;  break;
            case COREMESSAGING_VARIABLE_TYPE__INT32:   ValueContainer.Int =  * (int32_t*)  variable_pointer;  break;
            case COREMESSAGING_VARIABLE_TYPE__UINT32:  ValueContainer.Uint =  * (uint32_t*) variable_pointer;  break;
            default: break;
        }
    }
    else { //if ( CoreMessaging_getTypeClass( type ) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__FLOAT ) {
       #if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        if (type == COREMESSAGING_VARIABLE_TYPE__FLOAT)
       #endif
        { ValueContainer.Float = * (float*) variable_pointer; }
       #if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        else { //if (type == COREMESSAGING_VARIABLE_TYPE__DOUBLE)
            ValueContainer.Double = * (double*) variable_pointer;
        }
       #endif
    }
    return ValueContainer;
}*/

static uint16_t load_u16_le(const void *p)
{
    const uint8_t *b = (const uint8_t*)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t load_u32_le(const void *p)
{
    const uint8_t *b = (const uint8_t*)p;
    return (uint32_t)b[0]
        | ((uint32_t)b[1] << 8)
        | ((uint32_t)b[2] << 16)
        | ((uint32_t)b[3] << 24);
}

static int16_t load_i16_le(const void *p) { return (int16_t)load_u16_le(p); }
static int32_t load_i32_le(const void *p) { return (int32_t)load_u32_le(p); }

static float load_f32_le(const void *p)
{
    union { uint32_t u; float f; } v;
    v.u = load_u32_le(p);
    return v.f;
}

#if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
static uint64_t load_u64_le(const void *p)
{
    const uint8_t *b = (const uint8_t*)p;
    return (uint64_t)b[0]
        | ((uint64_t)b[1] << 8)
        | ((uint64_t)b[2] << 16)
        | ((uint64_t)b[3] << 24)
        | ((uint64_t)b[4] << 32)
        | ((uint64_t)b[5] << 40)
        | ((uint64_t)b[6] << 48)
        | ((uint64_t)b[7] << 56);
}

static double load_f64_le(const void *p)
{
    union { uint64_t u; double d; } v;
    v.u = load_u64_le(p);
    return v.d;
}
#endif

static inline CoreMessaging_ValueContainer
CoreMessaging_castVariableToValue(CoreMessaging_VariableTypes type, const void *p)
{
    CoreMessaging_ValueContainer vc = (CoreMessaging_ValueContainer){0};

    if (CoreMessaging_getTypeClass(type) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__INT) {
        switch (type) {
            case COREMESSAGING_VARIABLE_TYPE__BOOL:   vc.Int  = ((const uint8_t*)p)[0] ? 1 : 0; break;
            case COREMESSAGING_VARIABLE_TYPE__INT8:   vc.Int  = (int8_t)((const uint8_t*)p)[0]; break;
            case COREMESSAGING_VARIABLE_TYPE__UINT8:  vc.Int  = (uint8_t)((const uint8_t*)p)[0]; break;

            case COREMESSAGING_VARIABLE_TYPE__INT16:  vc.Int  = load_i16_le(p); break;
            case COREMESSAGING_VARIABLE_TYPE__UINT16: vc.Int  = (uint16_t)load_u16_le(p); break;

            case COREMESSAGING_VARIABLE_TYPE__INT32:  vc.Int  = load_i32_le(p); break;
            case COREMESSAGING_VARIABLE_TYPE__UINT32: vc.Uint = load_u32_le(p); break;

            default: break;
        }
    } else {
#if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        if (type == COREMESSAGING_VARIABLE_TYPE__FLOAT)
#endif
        { vc.Float = load_f32_le(p); }
#if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        else { vc.Double = load_f64_le(p); }
#endif
    }

    return vc;
}

__attribute__((always_inline))
static inline size_t CoreMessaging_castValueToVariable (CoreMessaging_VariableTypes type, CoreMessaging_ValueContainer value, void* variable_pointer) {
    if ( CoreMessaging_getTypeClass( type ) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__INT ) {
        switch (type) { //might take advantage of computed goto for faster operation
            case COREMESSAGING_VARIABLE_TYPE__BOOL:    if (variable_pointer!=NULL)  * (bool*)     variable_pointer  =  (bool)     value.Int;  return sizeof( bool );
            case COREMESSAGING_VARIABLE_TYPE__INT8:    if (variable_pointer!=NULL)  * (int8_t*)   variable_pointer  =  (int8_t)   value.Int;  return sizeof( int8_t );
            case COREMESSAGING_VARIABLE_TYPE__UINT8:   if (variable_pointer!=NULL)  * (uint8_t*)  variable_pointer  =  (uint8_t)  value.Int;  return sizeof( uint8_t );
            case COREMESSAGING_VARIABLE_TYPE__INT16:   if (variable_pointer!=NULL)  * (int16_t*)  variable_pointer  =  (int16_t)  value.Int;  return sizeof( int16_t );
            case COREMESSAGING_VARIABLE_TYPE__UINT16:  if (variable_pointer!=NULL)  * (uint16_t*) variable_pointer  =  (uint16_t) value.Int;  return sizeof( uint16_t );
            case COREMESSAGING_VARIABLE_TYPE__INT32:   if (variable_pointer!=NULL)  * (int32_t*)  variable_pointer  =  (int32_t)  value.Int;  return sizeof( int32_t );
            case COREMESSAGING_VARIABLE_TYPE__UINT32:  if (variable_pointer!=NULL)  * (uint32_t*) variable_pointer  =  (uint32_t) value.Uint;  return sizeof( uint32_t );
            default: break;
        }
    }
    else { //if ( CoreMessaging_getTypeClass( type ) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__FLOAT ) {
        switch (type) { //might take advantage of computed goto for faster operation
            case COREMESSAGING_VARIABLE_TYPE__FLOAT:   if (variable_pointer!=NULL)  * (float*)    variable_pointer  =  (float)    value.Float;  return sizeof( float );
           #if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
            case COREMESSAGING_VARIABLE_TYPE__DOUBLE:  if (variable_pointer!=NULL)  * (double*)   variable_pointer  =  (double)   value.Double;  return sizeof( double );
           #endif
            default: break;
        }
    }
    return 0; //keep GCC satisfied
}

/*static inline void store_u16_le(void *dst, uint16_t v)
{
    uint8_t *b = (uint8_t*)dst;
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
}

static inline void store_u32_le(void *dst, uint32_t v)
{
    uint8_t *b = (uint8_t*)dst;
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

#if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
static inline void store_u64_le(void *dst, uint64_t v)
{
    uint8_t *b = (uint8_t*)dst;
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    b[4] = (uint8_t)(v >> 32);
    b[5] = (uint8_t)(v >> 40);
    b[6] = (uint8_t)(v >> 48);
    b[7] = (uint8_t)(v >> 56);
}
#endif

__attribute__((always_inline))
static inline size_t
CoreMessaging_castValueToVariable(CoreMessaging_VariableTypes type,
                                  CoreMessaging_ValueContainer value,
                                  void *variable_pointer)
{
    // If caller passes NULL to ask "how many bytes would this type be?"
    if (variable_pointer == NULL) {
        return CoreMessaging_elemSize(type);
    }

    if (CoreMessaging_getTypeClass(type) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__INT) {
        switch (type) {
            case COREMESSAGING_VARIABLE_TYPE__BOOL:
            case COREMESSAGING_VARIABLE_TYPE__INT8:
            case COREMESSAGING_VARIABLE_TYPE__UINT8:
                ((uint8_t*)variable_pointer)[0] = (uint8_t)value.Int;
                return 1;

            case COREMESSAGING_VARIABLE_TYPE__INT16:
            case COREMESSAGING_VARIABLE_TYPE__UINT16:
                store_u16_le(variable_pointer, (uint16_t)value.Int);
                return 2;

            case COREMESSAGING_VARIABLE_TYPE__INT32:
                store_u32_le(variable_pointer, (uint32_t)(int32_t)value.Int);
                return 4;

            case COREMESSAGING_VARIABLE_TYPE__UINT32:
                store_u32_le(variable_pointer, (uint32_t)value.Uint);
                return 4;

            default:
                return 0;
        }
    } else {
        // Float / double
        if (type == COREMESSAGING_VARIABLE_TYPE__FLOAT) {
            union { uint32_t u; float f; } v;
            v.f = value.Float;
            store_u32_le(variable_pointer, v.u);
            return 4;
        }

#if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        // If you have a DOUBLE enum value, use it here. If not, keep as "else".
        union { uint64_t u; double d; } v;
        v.d = value.Double;
        store_u64_le(variable_pointer, v.u);
        return 8;
#else
        return 0;
#endif
    }
}*/

//Serializing variables/arrays/events for inter-CPU messaging:
#if ( ( defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) && SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 ) || ( defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) && SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 ) )
__attribute__((always_inline)) static inline void CoreMessaging_addByte (uint8_t value ) { CoreMessaging_Outgoing_Message[ CoreMessaging_Outgoing_ByteCount++ ] = value; }

__attribute__((always_inline)) static inline void CoreMessaging_addValue (CoreMessaging_VariableTypes type, CoreMessaging_ValueContainer value ) {
    CoreMessaging_Outgoing_ByteCount += CoreMessaging_castValueToVariable( type, value, &CoreMessaging_Outgoing_Message[ CoreMessaging_Outgoing_ByteCount ] );
}
#endif



__attribute__((always_inline))
static inline bool CoreMessaging_checkValueChange ( CoreMessaging_VariableTypes type, CoreMessaging_ValueContainer current_value, CoreMessaging_ValueContainer* previous_value__pointer ) {
    if ( CoreMessaging_getTypeClass( type ) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__INT ) {
        if (current_value.Uint != previous_value__pointer->Uint) {  previous_value__pointer->Uint = current_value.Uint; return true; } //equality-test with int32_t/uint32_t should work regardless of signed/unsigned nature (same bits compared by CPU)
    }
    else { //if ( CoreMessaging_getTypeClass( type ) == COREMESSAGING_VARIABLE_TYPE_CATEGORY__FLOAT ) {
       #if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        if (type == COREMESSAGING_VARIABLE_TYPE__FLOAT)
       #endif
        { if (current_value.Float != previous_value__pointer->Float) { previous_value__pointer->Float = current_value.Float; return true; } }
       #if (COREMESSAGING_VARIABLE_TYPE__DOUBLE__SUPPORT)
        else { //if (type == COREMESSAGING_VARIABLE_TYPE__DOUBLE)
            if (current_value.Double != previous_value__pointer->Double) { previous_value__pointer->Double = current_value.Double; return true; }
        }
       #endif
    }
    return false;
}

__attribute__((always_inline))
static inline void CoreMessaging_sendValue (int variable_index, CoreMessaging_VariableTypes type, CoreMessaging_ValueContainer value, int target_endpoint_index) {
   #if ( ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 ) && ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 ) )  //for now we don't want segfault with an uninitialized array of a single core
    static CoreMessaging_VariableDescriptor* TargetVariables;  //printf("Sending value %d to variable at index %d at endpoint %d\n", value.Int, variable_index, target_endpoint_index );
    TargetVariables = CoreMessaging_EndPoints[ target_endpoint_index ].Variables;
    CoreMessaging_castValueToVariable( type, value, TargetVariables[ variable_index ].VariablePointer );
    TargetVariables[ variable_index ].PreviousValue = value; //avoid triggering of a false 'change' at the endpoint
   #else
    CoreMessaging_addByte( COREMESSAGING_COMMAND__SET_VARIABLE ); CoreMessaging_addByte( variable_index ); CoreMessaging_addValue( type, value );
   #endif
}


__attribute__((always_inline))
static inline int CoreMessaging_checkArrayChange ( CoreMessaging_VariableDescriptor* variable_descriptors, int index, size_t array_size ) {
    static size_t j;
    for (j=0; j < array_size; ++j) { //detect for change
        if ( ( (uint8_t*) variable_descriptors[ index ].VariablePointer )[ j ] != ( (uint8_t*) variable_descriptors[ index ].PreviousValue.Pointer )[ j ] ) return j;
    }
    return -1;
}

__attribute__((always_inline))
static inline void CoreMessaging_sendArray (CoreMessaging_VariableDescriptor* variable_descriptors, int index, size_t start_index, size_t array_size, int target_endpoint_index) {
    static size_t j;
   #if ( ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 ) && ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 ) )  //for now we don't want segfault with an uninitialized array of a single core
    static CoreMessaging_VariableDescriptor* TargetVariables;
    TargetVariables = CoreMessaging_EndPoints[ target_endpoint_index ].Variables;
   #else
    CoreMessaging_addByte( COREMESSAGING_COMMAND__SET_VARIABLE ); CoreMessaging_addByte( index );
   #endif
    for (j = start_index /*0*/; j < array_size; ++j) { //update starting from the changed point
       #if ( ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 ) && ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 ) )  //for now we don't want segfault with an uninitialized array of a single core
        ( (uint8_t*) TargetVariables[ index ].VariablePointer )[ j ] =
       #else
        CoreMessaging_addByte( ( (uint8_t*) variable_descriptors[ index ].VariablePointer )[ j ] ); //CoreMessaging_addValue( type, value ); //byte-based approach is not OK for this
       #endif
        ( (uint8_t*) variable_descriptors[ index ].PreviousValue.Pointer )[ j ] = ( (uint8_t*) variable_descriptors[ index ].VariablePointer )[ j ]; //refresh array (or changed part of array if possible);
    }
}

void CoreMessaging_bindVariableTable(int endpoint_index,
                                     CoreMessaging_VariableDescriptor *vars)
{
    CoreMessaging_EndPoints[endpoint_index].Variables = vars;
}

void CoreMessaging_bindEventTable(int endpoint_index)
{
    CoreMessaging_EndPoints[endpoint_index].Events = CoreMessaging_Events[endpoint_index];

    for (int i = 0; i < COREMESSAGING_EVENT_AMOUNT; ++i) {
        CoreMessaging_EndPoints[endpoint_index].Events[i].callbackFunction = NULL;
        CoreMessaging_EndPoints[endpoint_index].Events[i].Triggered = false;
    }
}

void CoreMessaging_bindEventFunction (int event_id, CoreMessaging_callbackFunction callback_function, int endpoint_index) { //void (*callback_function)() ) {
    CoreMessaging_EndPoints[ endpoint_index ].Events[ event_id ].callbackFunction = callback_function;
}

__attribute__((always_inline))
static inline bool CoreMessaging_checkEventReceipt (int event_index, int endpoint_index) {
    static CoreMessaging_EventDescriptor *Event;
    Event = &CoreMessaging_EndPoints[ endpoint_index ].Events[ event_index ];
    if (Event->Triggered) { Event->Triggered = false; return true; }  else return false;
}

__attribute__((always_inline))
inline void CoreMessaging_sendEvent (int event_id, int target_endpoint_index) {
   #if ( ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 ) && ( !defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) || !SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 ) )  //for now we don't want segfault with an uninitialized array of a single core
    static CoreMessaging_EventDescriptor* TargetEvents;
    TargetEvents = CoreMessaging_EndPoints[ target_endpoint_index ].Events;
    TargetEvents[ event_id ].Triggered = true;
   #else
    CoreMessaging_addByte( COREMESSAGING_COMMAND__SEND_EVENT ); CoreMessaging_addByte( event_id ); //event-parameters are not needed currently in the project, just simple 'trigger'-like callbacks
   #endif
}

void CoreMessaging_callEvent (int event_id, int endpoint_index) { //for now no parameters are requested for the events
    static CoreMessaging_EventDescriptor Event;
    Event = CoreMessaging_EndPoints[ endpoint_index ].Events[ event_id ];
    if (Event.callbackFunction != NULL) ( Event.callbackFunction ) (); //call a local callback directly if registered
    //CoreMessaging_sendEvent( event_id, target_endpoint_index ); //message to the other core implicitly
}

void CoreMessaging_broadcastEvent (int event_id) { //send to all cores, and who has a non-NULL function-binding for the event_id, will call that function
    for (int i=0; i<COREMESSAGING_ENDPOINT_AMOUNT; ++i) CoreMessaging_sendEvent( event_id, i );
}



void CoreMessaging_init (CoreMessaging_VariableDescriptor* variable_descriptors, int endpoint_index) {
    static size_t ElementSize;
    LPRINTF( "ENTER:CoreMessagingDriver_init()\n" );
    fflush(stdout);
    CoreMessaging_EndPoints[ endpoint_index ].Variables = variable_descriptors;
    for (int i=0; variable_descriptors[i].Type != COREMESSAGING_VARIABLE_TYPE__END; ++i) {
        if (variable_descriptors[i].Count <= 1) CoreMessaging_castValueToVariable( variable_descriptors[i].Type, variable_descriptors[i].PreviousValue, variable_descriptors[i].VariablePointer ); //variable_descriptors[i].PreviousValue = CoreMessaging_castVariableToValue( variable_descriptors[i].Type, variable_descriptors[i].VariablePointer ); //The other way round? (Backend initializes the same variables on both sides.)
        else {
            ElementSize = CoreMessaging_castValueToVariable( variable_descriptors[i].Type, variable_descriptors[i].PreviousValue, NULL );
            variable_descriptors[i].PreviousValue.Pointer = (void*) malloc( variable_descriptors[i].Count * ElementSize );
            if (variable_descriptors[i].PreviousValue.Pointer != NULL) memcpy( variable_descriptors[i].PreviousValue.Pointer, variable_descriptors[i].VariablePointer, variable_descriptors[i].Count * ElementSize );
        }
    }
    CoreMessaging_EndPoints[ endpoint_index ].Events = CoreMessaging_Events[ endpoint_index ];
    for (int i = 0; i < COREMESSAGING_EVENT_AMOUNT; ++i) {
        CoreMessaging_EndPoints[endpoint_index].Events[i].callbackFunction = NULL;
        CoreMessaging_EndPoints[endpoint_index].Events[i].Triggered = false;
    }
LPRINTF("EXIT:CoreMessaging_init(ep=%d) vars=%p evts=%p\n",
        endpoint_index,
        (void*)CoreMessaging_EndPoints[endpoint_index].Variables,
        (void*)CoreMessaging_EndPoints[endpoint_index].Events);
fflush(stdout);
}


void CoreMessaging_refresh (CoreMessaging_VariableDescriptor* variable_descriptors, int endpoint_index, int target_endpoint_index) {
    static int i, ChangeIndex;
    static size_t ArraySize;
    static CoreMessaging_ValueContainer CurrentValue;
    LPRINTF("CoreMessaging_refresh\n");
    CoreMessaging_Outgoing_ByteCount = 0; //2; leave place for upcoming 'message-size' bytes, if messaging protocol doesn't add it or send length/delimiter

    for (i=0; variable_descriptors[i].Type != COREMESSAGING_VARIABLE_TYPE__END; ++i) {
        if (variable_descriptors[i].Count <= 1) { //check if single value changed
            CurrentValue = CoreMessaging_castVariableToValue( variable_descriptors[i].Type, variable_descriptors[i].VariablePointer );
            if ( CoreMessaging_checkValueChange( variable_descriptors[i].Type, CurrentValue, &variable_descriptors[i].PreviousValue ) ) {
                CoreMessaging_sendValue( i, variable_descriptors[i].Type, CurrentValue, target_endpoint_index );
            }
        }
        else { //check if any element of an array changed, if yes, send the changed portion (or full array at least)
            ArraySize = (size_t) variable_descriptors[i].Count * CoreMessaging_castValueToVariable( variable_descriptors[i].Type, variable_descriptors[i].PreviousValue, NULL ); //get array-size in bytes
            if ( ( ChangeIndex = CoreMessaging_checkArrayChange( variable_descriptors, i, ArraySize ) ) >= 0 ) {
                CoreMessaging_sendArray( variable_descriptors, i, ChangeIndex, ArraySize, target_endpoint_index );
            }
        }
    }
    for (i=0; i < COREMESSAGING_EVENT_AMOUNT; ++i) {
        if ( CoreMessaging_checkEventReceipt( i, endpoint_index ) ) CoreMessaging_callEvent( i, endpoint_index );
    }

//   #if ( ( defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_M33) && SQUARELINE_BUILD_TARGET__BOARD__CORE_M33 ) || ( defined(SQUARELINE_BUILD_TARGET__BOARD__CORE_A55) && SQUARELINE_BUILD_TARGET__BOARD__CORE_A55 ) )
//    CoreMessagingDriver_refresh();
//   #endif
}



