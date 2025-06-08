/******************************************************************************
 * Copyright (c) 2013, 2018 Potential Ventures Ltd
 * All rights reserved.
 */

 // 把gpi.h的内容合并到gpi_priv.h中
 #ifndef COCOTB_GPI_PRIV_H_
 #define COCOTB_GPI_PRIV_H_
 
 #define GPI_EXPORT 
  
 #include <map>
 #include <string>
 #include <vector>
 #include <string.h>

 
 class VpiCbHdl;
 // GpiImplInterface是为了支持vpi、vhpi、fli才需要进行的一层抽象, 这里我们
 // 只支持vpi。所以去除了具体的实现，而只保留一个空类（为了兼容已有代码）.
 class GPI_EXPORT GpiImplInterface {};
 class GpiIterator;
 class GpiObjHdl;
 typedef GpiObjHdl *gpi_sim_hdl;
 typedef VpiCbHdl *gpi_cb_hdl;
 typedef GpiIterator *gpi_iterator_hdl;
 
 // Stop the simulator
 GPI_EXPORT void gpi_sim_end(void); 
 
 // Returns simulation time as two uints. Units are default sim units
 GPI_EXPORT void gpi_get_sim_time(uint32_t *high, uint32_t *low);
 GPI_EXPORT void gpi_get_sim_precision(int32_t *precision);
 
 /**
  * Returns a string with the running simulator product information
  *
  * @return simulator product string
  */
 GPI_EXPORT const char *gpi_get_simulator_product(void);
 
 /**
  * Returns a string with the running simulator version
  *
  * @return simulator version string
  */
 GPI_EXPORT const char *gpi_get_simulator_version(void);
 
 // Functions for extracting a gpi_sim_hdl to an object
 // Returns a handle to the root simulation object.
 GPI_EXPORT gpi_sim_hdl gpi_get_root_handle(const char *name);
 GPI_EXPORT gpi_sim_hdl gpi_get_handle_by_name(gpi_sim_hdl parent,
                                               const char *name);
 GPI_EXPORT gpi_sim_hdl gpi_get_handle_by_index(gpi_sim_hdl parent,
                                                int32_t index);
 
 // Types that can be passed to the iterator.
 //
 // Note these are strikingly similar to the VPI types...
 typedef enum gpi_objtype_e {
     GPI_UNKNOWN = 0,
     GPI_MEMORY = 1,
     GPI_MODULE = 2,
     // GPI_NET = 3,  // Deprecated
     // GPI_PARAMETER = 4,  // Deprecated
     // GPI_REGISTER = 5,  // Deprecated
     GPI_ARRAY = 6,
     GPI_ENUM = 7,
     GPI_STRUCTURE = 8,
     GPI_REAL = 9,
     GPI_INTEGER = 10,
     GPI_STRING = 11,
     GPI_GENARRAY = 12,
     GPI_PACKAGE = 13,
     GPI_PACKED_STRUCTURE = 14,
     GPI_LOGIC = 15,
     GPI_LOGIC_ARRAY = 16,
 } gpi_objtype_t;
 
 // When iterating, we can chose to either get child objects, drivers or loads
 typedef enum gpi_iterator_sel_e {
     GPI_OBJECTS = 1,
     GPI_DRIVERS = 2,
     GPI_LOADS = 3,
     GPI_PACKAGE_SCOPES = 4,
 } gpi_iterator_sel_t;
 
 typedef enum gpi_set_action_e {
     GPI_DEPOSIT = 0,
     GPI_FORCE = 1,
     GPI_RELEASE = 2,
     GPI_NO_DELAY = 3,
 } gpi_set_action_t;

 typedef enum gpi_cb_state {
    GPI_FREE = 0,
    GPI_PRIMED = 1,
    GPI_CALL = 2,
    GPI_DELETE = 4,
} gpi_cb_state_e;
 
 // Functions for iterating over entries of a handle
 // Returns an iterator handle which can then be used in gpi_next calls
 //
 // Unlike `vpi_iterate` the iterator handle may only be NULL if the `type` is
 // not supported, If no objects of the requested type are found, an empty
 // iterator is returned.
 GPI_EXPORT gpi_iterator_hdl gpi_iterate(gpi_sim_hdl base,
                                         gpi_iterator_sel_t type);
 
 // Returns NULL when there are no more objects
 GPI_EXPORT gpi_sim_hdl gpi_next(gpi_iterator_hdl iterator);
 
 // Returns the number of objects in the collection of the handle
 GPI_EXPORT int gpi_get_num_elems(gpi_sim_hdl gpi_sim_hdl);
 
 // Returns the left side of the range constraint
 GPI_EXPORT int gpi_get_range_left(gpi_sim_hdl gpi_sim_hdl);
 
 // Returns the right side of the range constraint
 GPI_EXPORT int gpi_get_range_right(gpi_sim_hdl gpi_sim_hdl);
 
 // Returns the direction of the range constraint
 // +1 for ascending, -1 for descending, 0 for no direction
 GPI_EXPORT int gpi_get_range_dir(gpi_sim_hdl gpi_sim_hdl);
 
 // Functions for querying the properties of a handle
 // Caller responsible for freeing the returned string.
 // This is all slightly verbose but it saves having to enumerate various value
 // types We only care about a limited subset of values.
 GPI_EXPORT const char *gpi_get_signal_value_binstr(gpi_sim_hdl gpi_hdl);
 GPI_EXPORT const char *gpi_get_signal_name_str(gpi_sim_hdl gpi_hdl);
 GPI_EXPORT const char *gpi_get_signal_type_str(gpi_sim_hdl gpi_hdl);
 
 // Returns one of the types defined above e.g. gpiMemory etc.
 GPI_EXPORT gpi_objtype_t gpi_get_object_type(gpi_sim_hdl gpi_hdl);
 
 // Determine whether an object value is constant (parameters / generics etc)
 GPI_EXPORT int gpi_is_constant(gpi_sim_hdl gpi_hdl);
 
 // Determine whether an object is indexable
 GPI_EXPORT int gpi_is_indexable(gpi_sim_hdl gpi_hdl);
 
 // Functions for setting the properties of a handle
 GPI_EXPORT void gpi_set_signal_value_binstr(
     gpi_sim_hdl gpi_hdl, const char *str,
     gpi_set_action_t action);  // String of binary char(s) [1, 0, x, z]
 GPI_EXPORT void gpi_set_signal_value_int(gpi_sim_hdl gpi_hdl, int32_t value,
                                         gpi_set_action_t action);
 
 typedef enum gpi_edge {
     GPI_RISING,
     GPI_FALLING,
     GPI_VALUE_CHANGE,
 } gpi_edge_e;
 
 typedef enum gpi_range_dir {
     GPI_RANGE_DOWN = -1,
     GPI_RANGE_NO_DIR = 0,
     GPI_RANGE_UP = 1,
 } gpi_range_dir_e;
 
 // The callback registering functions
 GPI_EXPORT gpi_cb_hdl gpi_register_timed_callback(int (*gpi_function)(void *),
                                                   void *gpi_cb_data,
                                                   uint64_t time);
 GPI_EXPORT gpi_cb_hdl gpi_register_value_change_callback(
     int (*gpi_function)(void *), void *gpi_cb_data, gpi_sim_hdl gpi_hdl,
     gpi_edge_e edge);
 GPI_EXPORT gpi_cb_hdl
 gpi_register_readonly_callback(int (*gpi_function)(void *), void *gpi_cb_data);
 GPI_EXPORT gpi_cb_hdl
 gpi_register_nexttime_callback(int (*gpi_function)(void *), void *gpi_cb_data);
 GPI_EXPORT gpi_cb_hdl
 gpi_register_readwrite_callback(int (*gpi_function)(void *), void *gpi_cb_data);
 
 // Calling convention is that 0 = success and negative numbers a failure
 // For implementers of GPI the provided macro GPI_RET(x) is provided
 GPI_EXPORT void gpi_deregister_callback(gpi_cb_hdl gpi_hdl);
 
 // Because the internal structures may be different for different
 // implementations of GPI we provide a convenience function to extract the
 // callback data
 GPI_EXPORT void *gpi_get_callback_data(gpi_cb_hdl gpi_hdl);

 /* Base GPI class others are derived from */
 class GPI_EXPORT GpiHdl {
   public:
     GpiHdl(GpiImplInterface *impl, void *hdl = NULL)
         : m_impl(impl), m_obj_hdl(hdl) {}
     virtual ~GpiHdl() = default;
 
     template <typename T>
     T get_handle() const {
         return static_cast<T>(m_obj_hdl);
     }
 
   private:
     GpiHdl() {}  // Disable default constructor
 
   public:
     GpiImplInterface *m_impl;                   // VPI/VHPI/FLI routines
     bool is_this_impl(GpiImplInterface *impl);  // Is the passed interface the
                                                 // one this object uses?
 
   protected:
     void *m_obj_hdl;
 };
 
 /* GPI object handle, maps to a simulation object */
 // An object is any item in the hierarchy
 // Provides methods for iterating through children or finding by name
 // Initial object is returned by call to GpiImplInterface::get_root_handle()
 // Subsequent operations to get children go through this handle.
 // GpiObjHdl::get_handle_by_name/get_handle_by_index are really factories
 // that construct an object derived from GpiSignalObjHdl or GpiObjHdl
 class GPI_EXPORT GpiObjHdl : public GpiHdl {
   public:
     GpiObjHdl(GpiImplInterface *impl, void *hdl = nullptr,
               gpi_objtype_t objtype = GPI_UNKNOWN, bool is_const = false)
         : GpiHdl(impl, hdl), m_type(objtype), m_const(is_const) {}
 
     virtual ~GpiObjHdl() = default;
 
     virtual const char *get_name_str() { return m_name.c_str(); };
     virtual const char *get_fullname_str() { return m_fullname.c_str(); };
     virtual const char *get_type_str();
     gpi_objtype_t get_type() { return m_type; };
     bool get_const() { return m_const; };
     int get_num_elems() {
        //  LOG_DEBUG("%s has %d elements", m_name.c_str(), m_num_elems);
         return m_num_elems;
     }
     int get_range_left() { return m_range_left; }
     int get_range_right() { return m_range_right; }
     gpi_range_dir get_range_dir() {
        //  LOG_DEBUG("%s has direction %d", m_name.c_str(), m_range_dir);
         return m_range_dir;
     }
     int get_indexable() { return m_indexable; }
 
     const std::string &get_name() { return m_name; };
     const std::string &get_fullname() { return m_fullname; };
 
     virtual const char *get_definition_name() {
         return m_definition_name.c_str();
     };
 
     bool is_native_impl(GpiImplInterface *impl);
     virtual int initialise(const std::string &name,
                            const std::string &full_name);
 
   protected:
     int m_num_elems = 0;
     bool m_indexable = false;
     int m_range_left = -1;
     int m_range_right = -1;
     gpi_range_dir m_range_dir = GPI_RANGE_NO_DIR;
     std::string m_name = "unknown";
     std::string m_fullname = "unknown";
 
     std::string m_definition_name;
     std::string m_definition_file;
 
     gpi_objtype_t m_type;
     bool m_const;
 };
 
 /* GPI Signal object handle, maps to a simulation object */
 //
 // Identical to an object but adds additional methods for getting/setting the
 // value of the signal (which doesn't apply to non signal items in the hierarchy
 class GPI_EXPORT GpiSignalObjHdl : public GpiObjHdl {
   public:
     using GpiObjHdl::GpiObjHdl;
 
     virtual ~GpiSignalObjHdl() = default;
     // Provide public access to the implementation (composition vs inheritance)
     virtual const char *get_signal_value_binstr() = 0;
 
     int m_length = 0;
 
     virtual int set_signal_value(const int32_t value,
                                  gpi_set_action_t action) = 0;
     virtual int set_signal_value(const double value,
                                  gpi_set_action_t action) = 0;
     virtual int set_signal_value_binstr(std::string &value,
                                         gpi_set_action_t action) = 0;
 
     virtual VpiCbHdl *register_value_change_callback(
         gpi_edge_e edge, int (*gpi_function)(void *), void *gpi_cb_data) = 0;
 };
 
 class VpiSignalObjHdl : public GpiSignalObjHdl {
  public:
    VpiSignalObjHdl(GpiImplInterface *impl, vpiHandle hdl,
                    gpi_objtype_t objtype, bool is_const)
        : GpiSignalObjHdl(impl, hdl, objtype, is_const) {}

    const char *get_signal_value_binstr() override;

    int set_signal_value(const int32_t value, gpi_set_action_t action) override;
    int set_signal_value(const double value, gpi_set_action_t action) override;
    int set_signal_value_binstr(std::string &value,
                                gpi_set_action_t action) override;

    /* Value change callback accessor */
    int initialise(const std::string &name,
                   const std::string &fq_name) override;
    VpiCbHdl *register_value_change_callback(gpi_edge_e edge,
                                             int (*function)(void *),
                                             void *cb_data) override;

  private:
    int set_signal_value(s_vpi_value value, gpi_set_action_t action);
};

class VpiArrayObjHdl : public GpiObjHdl {
  public:
    VpiArrayObjHdl(GpiImplInterface *impl, vpiHandle hdl, gpi_objtype_t objtype)
        : GpiObjHdl(impl, hdl, objtype) {}

    int initialise(const std::string &name,
                   const std::string &fq_name) override;
};
 
 class GPI_EXPORT GpiIterator : public GpiHdl {
   public:
     enum Status {
         NATIVE,          // Fully resolved object was created
         NATIVE_NO_NAME,  // Native object was found but unable to fully create
         NOT_NATIVE,      // Non-native object was found but we did get a name
         NOT_NATIVE_NO_NAME,  // Non-native object was found without a name
         END
     };
 
     GpiIterator(GpiImplInterface *impl, GpiObjHdl *hdl)
         : GpiHdl(impl), m_parent(hdl) {}
     virtual ~GpiIterator() = default;
 
     virtual Status next_handle(std::string &name, GpiObjHdl **hdl, void **) {
         name = "";
         *hdl = NULL;
         return GpiIterator::END;
     }
 
     GpiObjHdl *get_parent() { return m_parent; }
 
   protected:
     GpiObjHdl *m_parent;
 };
 
 /* Called from implementation layers back up the stack */
 GPI_EXPORT int gpi_register_impl(GpiImplInterface *func_tbl);
 
//  GPI_EXPORT void gpi_embed_init(int argc, char const *const *argv);
//  GPI_EXPORT void gpi_embed_end();
//  GPI_EXPORT void gpi_entry_point();
//  GPI_EXPORT void gpi_to_user();
//  GPI_EXPORT void gpi_to_simulator();
 
 typedef void (*layer_entry_func)();
 
#define check_vpi_error() do {;} while (0)


 #endif /* COCOTB_GPI_PRIV_H_ */
 