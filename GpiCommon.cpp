#include "VpiImpl.h"
#include "gpi_priv.h"
#include <vpi_user.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace std;
static vector<GpiImplInterface *> registered_impls;

#define CHECK_AND_STORE(_x) _x
#define CLEAR_STORE() (void)0  // No-op

void gpi_get_sim_time(uint32_t *high, uint32_t *low) {
    s_vpi_time vpi_time_s;
    vpi_time_s.type = vpiSimTime;  // vpiSimTime;
    vpi_get_time(NULL, &vpi_time_s);
    // check_vpi_error();
    *high = vpi_time_s.high;
    *low = vpi_time_s.low;
}

void gpi_get_sim_precision(int32_t *precision) {
    *precision = vpi_get(vpiTimePrecision, NULL);
}

const char *gpi_get_simulator_product() {
    return "icarus";
}

const char *gpi_get_simulator_version() {
    return "unknown";
}

const char *GpiObjHdl::get_type_str() {
#define CASE_OPTION(_X) \
    case _X:            \
        ret = #_X;      \
        break

    const char *ret;

    switch (m_type) {
        CASE_OPTION(GPI_UNKNOWN);
        CASE_OPTION(GPI_MEMORY);
        CASE_OPTION(GPI_MODULE);
        CASE_OPTION(GPI_ARRAY);
        CASE_OPTION(GPI_ENUM);
        CASE_OPTION(GPI_STRUCTURE);
        CASE_OPTION(GPI_REAL);
        CASE_OPTION(GPI_INTEGER);
        CASE_OPTION(GPI_STRING);
        CASE_OPTION(GPI_GENARRAY);
        CASE_OPTION(GPI_PACKAGE);
        CASE_OPTION(GPI_LOGIC);
        CASE_OPTION(GPI_LOGIC_ARRAY);
        default:
            ret = "unknown";
    }

    return ret;
}

int GpiObjHdl::initialise(const std::string &name, const std::string &fq_name) {
    m_name = name;
    m_fullname = fq_name;
    return 0;
}

static gpi_objtype_t to_gpi_objtype(int32_t vpitype, int num_elements = 0,
                                    bool is_vector = false) {
    switch (vpitype) {
        case vpiNet:
        case vpiReg:
        case vpiMemoryWord:
            if (is_vector || num_elements > 1) {
            return GPI_LOGIC_ARRAY;
            } else {
            return GPI_LOGIC;
            }
            break;

        case vpiRealVar:
            return GPI_REAL;

        case vpiRegArray:
        case vpiNetArray:
        case vpiMemory:
            return GPI_ARRAY;

        case vpiIntegerVar:
            return GPI_INTEGER;

        case vpiModule:
        case vpiPort:
        case vpiGenScope:
            return GPI_MODULE;

        case vpiStringVal:
            return GPI_STRING;

        default:
            LOG_DEBUG("Unable to map VPI type %d onto GPI type", vpitype);
            return GPI_UNKNOWN;
    }
}

// 获取verilog中的对象，会在python端作为入口，然后跳入simulatormodule，
// 在然后在这里实际调用vpi接口来获取/设置数据
GpiObjHdl *get_root_handle(const char *name) {
    vpiHandle root;
    vpiHandle iterator;
    GpiObjHdl *rv;
    std::string root_name;

    // vpi_iterate with a ref of NULL returns the top level module
    iterator = vpi_iterate(vpiModule, NULL);
    check_vpi_error();
    if (!iterator) {
        LOG_INFO("Nothing visible via VPI");
        return NULL;
    }

    for (root = vpi_scan(iterator); root != NULL; root = vpi_scan(iterator)) {
        if (to_gpi_objtype(vpi_get(vpiType, root)) != GPI_MODULE) continue;

        if (name == NULL || !strcmp(name, vpi_get_str(vpiFullName, root)))
            break;
    }

    if (!root) {
        check_vpi_error();
        goto error;
    }

    // Need to free the iterator if it didn't return NULL
    if (iterator && !vpi_free_object(iterator)) {
        LOG_WARN("VPI: Attempting to free root iterator failed!");
        check_vpi_error();
    }

    root_name = vpi_get_str(vpiFullName, root);
    rv = new GpiObjHdl(NULL, root, to_gpi_objtype(vpi_get(vpiType, root)));
    rv->initialise(root_name, root_name);

    return rv;

error:

    LOG_ERROR("VPI: Couldn't find root handle %s", name);

    iterator = vpi_iterate(vpiModule, NULL);

    for (root = vpi_scan(iterator); root != NULL; root = vpi_scan(iterator)) {
        LOG_ERROR("VPI: Toplevel instances: %s != %s...", name,
                  vpi_get_str(vpiFullName, root));

        if (name == NULL || !strcmp(name, vpi_get_str(vpiFullName, root)))
            break;
    }

    return NULL;
}

gpi_sim_hdl gpi_get_root_handle(const char *name) {
    GpiObjHdl *hdl = NULL;

    LOG_DEBUG("Looking for root handle '%s'", name);
    if ((hdl = get_root_handle(name))) {
        LOG_DEBUG("Got a Root handle (%s)", hdl->get_name_str());
    }

    if (hdl)
        return CHECK_AND_STORE(hdl);
    else {
        LOG_ERROR("No root handle found");
        return hdl;
    }
}

const char* get_type_delimiter(GpiObjHdl *obj_hdl) {
    return (obj_hdl->get_type() == GPI_PACKAGE) ? "" : ".";
}

bool compare_generate_labels(const std::string &a,
                            const std::string &b) {
    /* Compare two generate labels for equality ignoring any suffixed index. */
    std::size_t a_idx = a.rfind("[");
    std::size_t b_idx = b.rfind("[");
    return a.substr(0, a_idx) == b.substr(0, b_idx);
}

static gpi_objtype_t const_type_to_gpi_objtype(int32_t const_type) {
    // Most simulators only return vpiDecConst or vpiBinaryConst
    switch (const_type) {
#ifdef IUS
        case vpiUndefined:
            LOG_WARN(
                "VPI: Xcelium reports undefined parameters as vpiUndefined, "
                "guessing this is a logic vector");
            return GPI_LOGIC_ARRAY;
#endif
        case vpiDecConst:
        case vpiBinaryConst:
        case vpiOctConst:
        case vpiHexConst:
            return GPI_LOGIC_ARRAY;
        case vpiRealConst:
            return GPI_REAL;
        case vpiStringConst:
            return GPI_STRING;
        // case vpiTimeConst:  // Not implemented
        default:
            LOG_DEBUG("Unable to map vpiConst type %d onto GPI type",
                      const_type);
            return GPI_UNKNOWN;
    }
}

GpiObjHdl* create_gpi_obj_from_handle(vpiHandle new_hdl,
                                      const std::string &name,
                                      const std::string &fq_name) {
    int32_t type;
    GpiObjHdl *new_obj = NULL;
    // icarus 中没有找到vpiUnknown，临时定义
    const int vpiUnknown = 3;
    if (vpiUnknown == (type = vpi_get(vpiType, new_hdl))) {
        LOG_DEBUG("vpiUnknown returned from vpi_get(vpiType, ...)");
        return NULL;
    }

    /* What sort of instance is this ?*/
    switch (type) {
        case vpiNet:
        case vpiReg:
        case vpiIntegerVar:
        case vpiRealVar:
        case vpiMemoryWord: 
        {
            const auto is_vector = vpi_get(vpiVector, new_hdl);
            const auto num_elements = vpi_get(vpiSize, new_hdl);
            new_obj = new VpiSignalObjHdl(
                NULL, new_hdl, to_gpi_objtype(type, num_elements, is_vector),
                false);
            break;
        }
        case vpiParameter:
        case vpiConstant: {
            auto const_type = vpi_get(vpiConstType, new_hdl);
            new_obj = new VpiSignalObjHdl(
                NULL, new_hdl, const_type_to_gpi_objtype(const_type), true);
            break;
        }
        case vpiRegArray:
        case vpiNetArray:
        case vpiMemory:{
            const auto is_vector = vpi_get(vpiVector, new_hdl);
            const auto num_elements = vpi_get(vpiSize, new_hdl);
            new_obj = new VpiArrayObjHdl(
                NULL, new_hdl, to_gpi_objtype(type, num_elements, is_vector));
            break;
        }
        case vpiModule:
        case vpiPort:
        case vpiGenScope: {
            std::string hdl_name = vpi_get_str(vpiName, new_hdl);

            if (hdl_name != name) {
                LOG_DEBUG("Found pseudo-region %s (hdl_name=%s but name=%s)",
                          fq_name.c_str(), hdl_name.c_str(), name.c_str());
                new_obj = new GpiObjHdl(NULL, new_hdl, GPI_GENARRAY);
            } else {
                new_obj = new GpiObjHdl(NULL, new_hdl, to_gpi_objtype(type));
            }
            break;
        }
        default:
            /* We should only print a warning here if the type is really
               Verilog, It could be VHDL as some simulators allow querying of
               both languages via the same handle
               */
            const char *type_name = vpi_get_str(vpiType, new_hdl);
            std::string unknown = "vpiUnknown";
            if (type_name && (unknown != type_name)) {
                LOG_WARN("VPI: Not able to map type %s(%d) to object.",
                         type_name, type);
            } else {
                LOG_WARN("VPI: Simulator does not know this type (%d) via VPI",
                         type);
            }
            return NULL;
    }

    new_obj->initialise(name, fq_name);

    LOG_DEBUG("VPI: Created GPI object from type %s(%d)",
              vpi_get_str(vpiType, new_hdl), type);

    return new_obj;
}

GpiObjHdl* native_check_create(const std::string &name,
                              GpiObjHdl *parent) {
    const vpiHandle parent_hdl = parent->get_handle<vpiHandle>();
    std::string fq_name =
        parent->get_fullname() + get_type_delimiter(parent) + name;

    vpiHandle new_hdl =
        vpi_handle_by_name(const_cast<char *>(fq_name.c_str()), NULL);

#ifdef IUS
    if (new_hdl != NULL && vpi_get(vpiType, new_hdl) == vpiGenScope) {
        // verify that this xcelium scope is valid, or else we segfault on the
        // invalid scope. Xcelium only returns vpiGenScope, no vpiGenScopeArray

        vpiHandle iter = vpi_iterate(vpiInternalScope, parent_hdl);
        bool is_valid = [&]() -> bool {
            for (auto rgn = vpi_scan(iter); rgn != NULL; rgn = vpi_scan(iter)) {
                if (compare_generate_labels(vpi_get_str(vpiName, rgn),
                                                     name)) {
                    return true;
                }
            }
            return false;
        }();
        vpi_free_object(iter);

        if (!is_valid) {
            vpi_free_object(new_hdl);
            new_hdl = NULL;
        }
    }
#endif

// Xcelium will segfault on a scope that doesn't exist
#ifndef IUS
    /* Some simulators do not support vpiGenScopeArray, only vpiGenScope:
     * - Icarus Verilog
     * - Verilator
     * - Questa/Modelsim
     *
     * If handle is not found by name, look for a generate block with
     * a matching prefix.
     *     For Example:
     *         genvar idx;
     *         generate
     *             for (idx = 0; idx < 5; idx = idx + 1) begin
     *                 ...
     *             end
     *         endgenerate
     *
     *     genblk1      => vpiGenScopeArray (not found)
     *     genblk1[0]   => vpiGenScope
     *     ...
     *     genblk1[4]   => vpiGenScope
     *
     *     genblk1 is not found directly, but if genblk1[n] is found,
     *     genblk1 must exist, so create the pseudo-region object for it.
     */
    if (new_hdl == NULL) {
        LOG_DEBUG(
            "Unable to find '%s' through vpi_handle_by_name, looking for "
            "matching generate scope array using fallback",
            fq_name.c_str());

        vpiHandle iter = vpi_iterate(vpiInternalScope, parent_hdl);
        if (iter != NULL) {
            for (auto rgn = vpi_scan(iter); rgn != NULL; rgn = vpi_scan(iter)) {
                auto rgn_type = vpi_get(vpiType, rgn);
                if (rgn_type == vpiGenScope || rgn_type == vpiModule) {
                    std::string rgn_name = vpi_get_str(vpiName, rgn);
                    if (compare_generate_labels(rgn_name, name)) {
                        new_hdl = parent_hdl;
                        vpi_free_object(iter);
                        break;
                    }
                }
            }
        }
    }
#endif

    if (new_hdl == NULL) {
        LOG_DEBUG("Unable to find '%s'", fq_name.c_str());
        return NULL;
    }

    /* Generate Loops have inconsistent behavior across vpi tools.  A "name"
     * without an index, i.e. dut.loop vs dut.loop[0], will find a handle to
     * vpiGenScopeArray, but not all tools support iterating over the
     * vpiGenScopeArray.  We don't want to create a GpiObjHdl to this type of
     * vpiHandle.
     *
     * If this unique case is hit, we need to create the Pseudo-region, with the
     * handle being equivalent to the parent handle.
     */
    // icarus并不支持vpiGenScopeArray，头文件里也没有这个定义。这里临时补充定义
    const int vpiGenScopeArray = 133;
    if (vpi_get(vpiType, new_hdl) == vpiGenScopeArray) {
        vpi_free_object(new_hdl);

        new_hdl = parent_hdl;
    }

    GpiObjHdl *new_obj = create_gpi_obj_from_handle(new_hdl, name, fq_name);
    if (new_obj == NULL) {
        vpi_free_object(new_hdl);
        LOG_DEBUG("Unable to create object '%s'", fq_name.c_str());
        return NULL;
    }
    return new_obj;
}

GpiObjHdl* native_check_create(int32_t index, GpiObjHdl *parent) {
    vpiHandle vpi_hdl = parent->get_handle<vpiHandle>();
    vpiHandle new_hdl = NULL;

    char buff[14];  // needs to be large enough to hold -2^31 to 2^31-1 in
                    // string form ('['+'-'10+']'+'\0')

    gpi_objtype_t obj_type = parent->get_type();

    if (obj_type == GPI_GENARRAY) {
        snprintf(buff, 14, "[%d]", index);

        LOG_DEBUG(
            "Native check create for index %d of parent '%s' (pseudo-region)",
            index, parent->get_name_str());

        std::string idx = buff;
        std::string hdl_name = parent->get_fullname() + idx;
        std::vector<char> writable(hdl_name.begin(), hdl_name.end());
        writable.push_back('\0');

        new_hdl = vpi_handle_by_name(&writable[0], NULL);
    } else if (obj_type == GPI_LOGIC || obj_type == GPI_LOGIC_ARRAY ||
               obj_type == GPI_ARRAY || obj_type == GPI_STRING) {
        new_hdl = vpi_handle_by_index(vpi_hdl, index);

        /* vpi_handle_by_index() doesn't work for all simulators when dealing
         * with a two-dimensional array. For example: wire [7:0] sig_t4
         * [0:1][0:2];
         *
         *    Assume vpi_hdl is for "sig_t4":
         *       vpi_handle_by_index(vpi_hdl, 0);   // Returns a handle to
         * sig_t4[0] for IUS, but NULL on Questa
         *
         *    Questa only works when both indices are provided, i.e. will need a
         * pseudo-handle to behave like the first index.
         */
        if (new_hdl == NULL) {
            int left = parent->get_range_left();
            int right = parent->get_range_right();
            bool ascending = parent->get_range_dir() == GPI_RANGE_UP;

            LOG_DEBUG(
                "Unable to find handle through vpi_handle_by_index(), "
                "attempting second method");

            if ((ascending && (index < left || index > right)) ||
                (!ascending && (index > left || index < right))) {
                LOG_ERROR(
                    "Invalid Index - Index %d is not in the range of [%d:%d]",
                    index, left, right);
                return NULL;
            }

            /* Get the number of constraints to determine if the index will
             * result in a pseudo-handle or should be found */
            vpiHandle p_hdl = parent->get_handle<vpiHandle>();
            const int vpiRange = 115;
            vpiHandle it = vpi_iterate(vpiRange, p_hdl);
            int constraint_cnt = 0;
            if (it != NULL) {
                while (vpi_scan(it) != NULL) {
                    ++constraint_cnt;
                }
            } else {
                constraint_cnt = 1;
            }

            std::string act_hdl_name = vpi_get_str(vpiName, p_hdl);

            /* Removing the act_hdl_name from the parent->get_name() will leave
             * the pseudo-indices */
            if (act_hdl_name.length() < parent->get_name().length()) {
                std::string idx_str =
                    parent->get_name().substr(act_hdl_name.length());

                while (idx_str.length() > 0) {
                    std::size_t found = idx_str.find_first_of("]");

                    if (found != std::string::npos) {
                        --constraint_cnt;
                        idx_str = idx_str.substr(found + 1);
                    } else {
                        break;
                    }
                }
            }

            snprintf(buff, 14, "[%d]", index);

            std::string idx = buff;
            std::string hdl_name = parent->get_fullname() + idx;

            std::vector<char> writable(hdl_name.begin(), hdl_name.end());
            writable.push_back('\0');

            new_hdl = vpi_handle_by_name(&writable[0], NULL);

            /* Create a pseudo-handle if not the last index into a
             * multi-dimensional array */
            if (new_hdl == NULL && constraint_cnt > 1) {
                new_hdl = p_hdl;
            }
        }
    } else {
        LOG_ERROR(
            "VPI: Parent of type %s must be of type GPI_GENARRAY, "
            "GPI_LOGIC, GPI_LOGIC, GPI_ARRAY, or GPI_STRING to have an index.",
            parent->get_type_str());
        return NULL;
    }

    if (new_hdl == NULL) {
        LOG_DEBUG("Unable to vpi_get_handle_by_index %s[%d]",
                  parent->get_name_str(), index);
        return NULL;
    }

    snprintf(buff, 14, "[%d]", index);

    std::string idx = buff;
    std::string name = parent->get_name() + idx;
    std::string fq_name = parent->get_fullname() + idx;
    GpiObjHdl *new_obj = create_gpi_obj_from_handle(new_hdl, name, fq_name);
    if (new_obj == NULL) {
        vpi_free_object(new_hdl);
        LOG_DEBUG("Unable to fetch object below entity (%s) at index (%d)",
                  parent->get_name_str(), index);
        return NULL;
    }
    return new_obj;
}

GpiObjHdl* native_check_create(void *raw_hdl, GpiObjHdl *parent) {
    LOG_DEBUG("Trying to convert raw to VPI handle");

    vpiHandle new_hdl = (vpiHandle)raw_hdl;

    const char *c_name = vpi_get_str(vpiName, new_hdl);
    if (!c_name) {
        LOG_DEBUG("Unable to query name of passed in handle");
        return NULL;
    }

    std::string name = c_name;
    std::string fq_name =
        parent->get_fullname() + get_type_delimiter(parent) + name;

    GpiObjHdl *new_obj = create_gpi_obj_from_handle(new_hdl, name, fq_name);
    if (new_obj == NULL) {
        vpi_free_object(new_hdl);
        LOG_DEBUG("Unable to fetch object %s", fq_name.c_str());
        return NULL;
    }
    return new_obj;
}

static GpiObjHdl *gpi_get_handle_by_name_(GpiObjHdl *parent,
                                          const std::string &name,
                                          GpiImplInterface *skip_impl) {
    LOG_DEBUG("Searching for %s", name.c_str());
    auto hdl = native_check_create(name, parent);
    if (hdl) {
        return CHECK_AND_STORE(hdl);
    }

    return NULL;
}

static GpiObjHdl *gpi_get_handle_by_raw(GpiObjHdl *parent, void *raw_hdl,
                                        GpiImplInterface *skip_impl) {
    auto hdl = native_check_create(raw_hdl, parent);
    if (hdl) {
        return CHECK_AND_STORE(hdl);
    }

    return NULL;
}

gpi_sim_hdl gpi_get_handle_by_name(gpi_sim_hdl base, const char *name) {
    std::string s_name = name;
    GpiObjHdl *hdl = gpi_get_handle_by_name_(base, s_name, NULL);
    if (!hdl) {
        LOG_DEBUG(
            "Failed to find a handle named %s via any registered "
            "implementation",
            name);
    }
    return hdl;
}

gpi_sim_hdl gpi_get_handle_by_index(gpi_sim_hdl base, int32_t index) {
    GpiObjHdl *hdl = NULL;

    /* Shouldn't need to iterate over interfaces because indexing into a handle
     * shouldn't cross the interface boundaries.
     *
     * NOTE: IUS's VPI interface returned valid VHDL handles, but then couldn't
     *       use the handle properly.
     */
    hdl = native_check_create(index, base);

    if (hdl)
        return CHECK_AND_STORE(hdl);
    else {
        LOG_WARN(
            "Failed to find a handle at index %d via any registered "
            "implementation",
            index);
        return hdl;
    }
}

gpi_iterator_hdl gpi_iterate(gpi_sim_hdl obj_hdl, gpi_iterator_sel_t type) {
    // 这里并没有真正实现对于vpi_iterate的支持，所以直接返回NULL
    return NULL;
}

gpi_sim_hdl gpi_next(gpi_iterator_hdl iter) {
    std::string name;
    GpiObjHdl *parent = iter->get_parent();

    while (true) {
        GpiObjHdl *next = NULL;
        void *raw_hdl = NULL;
        GpiIterator::Status ret = iter->next_handle(name, &next, &raw_hdl);

        switch (ret) {
            case GpiIterator::NATIVE:
                LOG_DEBUG("Create a native handle");
                return CHECK_AND_STORE(next);
            case GpiIterator::NATIVE_NO_NAME:
                LOG_DEBUG("Unable to fully setup handle, skipping");
                continue;
            case GpiIterator::NOT_NATIVE:
                LOG_DEBUG(
                    "Found a name but unable to create via native "
                    "implementation, trying others");
                next = gpi_get_handle_by_name_(parent, name, iter->m_impl);
                if (next) {
                    return next;
                }
                LOG_WARN(
                    "Unable to create %s via any registered implementation",
                    name.c_str());
                continue;
            case GpiIterator::NOT_NATIVE_NO_NAME:
                next = gpi_get_handle_by_raw(parent, raw_hdl, iter->m_impl);
                if (next) {
                    return next;
                }
                continue;
            case GpiIterator::END:
                LOG_DEBUG("Reached end of iterator");
                delete iter;
                return NULL;
        }
    }
}


static std::string g_binstr;

const char *gpi_get_signal_value_binstr(gpi_sim_hdl sig_hdl) {
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    g_binstr = obj_hdl->get_signal_value_binstr();
    std::transform(g_binstr.begin(), g_binstr.end(), g_binstr.begin(),
                   ::toupper);
    return g_binstr.c_str();
}

const char *gpi_get_signal_name_str(gpi_sim_hdl sig_hdl) {
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    return obj_hdl->get_name_str();
}

const char *gpi_get_signal_type_str(gpi_sim_hdl obj_hdl) {
    return obj_hdl->get_type_str();
}

gpi_objtype_t gpi_get_object_type(gpi_sim_hdl obj_hdl) {
    return obj_hdl->get_type();
}

int gpi_is_constant(gpi_sim_hdl obj_hdl) {
    if (obj_hdl->get_const()) return 1;
    return 0;
}

int gpi_is_indexable(gpi_sim_hdl obj_hdl) {
    if (obj_hdl->get_indexable()) return 1;
    return 0;
}

void gpi_set_signal_value_int(gpi_sim_hdl sig_hdl, int32_t value,
                              gpi_set_action_t action) {
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);

    obj_hdl->set_signal_value(value, action);
}

void gpi_set_signal_value_binstr(gpi_sim_hdl sig_hdl, const char *binstr,
                                 gpi_set_action_t action) {
    std::string value = binstr;
    GpiSignalObjHdl *obj_hdl = static_cast<GpiSignalObjHdl *>(sig_hdl);
    obj_hdl->set_signal_value_binstr(value, action);
}


int gpi_get_num_elems(gpi_sim_hdl obj_hdl) { return obj_hdl->get_num_elems(); }

int gpi_get_range_left(gpi_sim_hdl obj_hdl) {
    return obj_hdl->get_range_left();
}

int gpi_get_range_right(gpi_sim_hdl obj_hdl) {
    return obj_hdl->get_range_right();
}

int gpi_get_range_dir(gpi_sim_hdl obj_hdl) {
    return static_cast<int>(obj_hdl->get_range_dir());
}

gpi_cb_hdl gpi_register_value_change_callback(int (*gpi_function)(void *),
                                              void *gpi_cb_data,
                                              gpi_sim_hdl sig_hdl,
                                              gpi_edge_e edge) {
    VpiSignalObjHdl *signal_hdl = static_cast<VpiSignalObjHdl *>(sig_hdl);

    /* Do something based on int & GPI_RISING | GPI_FALLING */
    VpiCbHdl *gpi_hdl = signal_hdl->register_value_change_callback(
        edge, gpi_function, gpi_cb_data);
    if (!gpi_hdl) {
        LOG_ERROR("Failed to register a value change callback");
        return NULL;
    } else {
        return gpi_hdl;
    }
}

gpi_cb_hdl gpi_register_timed_callback(int (*gpi_function)(void *),
                                       void *gpi_cb_data, uint64_t time) {
    VpiTimedCbHdl *hdl = new VpiTimedCbHdl(time);

    if (hdl->arm_callback()) {
        delete (hdl);
        return NULL;
    }
    hdl->set_user_data(gpi_function, gpi_cb_data);
    return (gpi_cb_hdl)hdl;
}

VpiNextPhaseCbHdl m_next_phase;
gpi_cb_hdl gpi_register_nexttime_callback(int (*gpi_function)(void *),
                                          void *gpi_cb_data) {
    if (m_next_phase.arm_callback()) return NULL;
    m_next_phase.set_user_data(gpi_function, gpi_cb_data);
    return (gpi_cb_hdl)&m_next_phase;
}

VpiReadWriteCbHdl m_read_write;
gpi_cb_hdl gpi_register_readwrite_callback(int (*gpi_function)(void *),
                                           void *gpi_cb_data) {
    if (m_read_write.arm_callback()) return NULL;
    m_read_write.set_user_data(gpi_function, gpi_cb_data);
    return (gpi_cb_hdl)&m_read_write;
}

VpiReadOnlyCbHdl m_read_only;
gpi_cb_hdl gpi_register_readonly_callback(int (*gpi_function)(void *),
                                          void *gpi_cb_data) {
    if (m_read_only.arm_callback()) return NULL;
    m_read_only.set_user_data(gpi_function, gpi_cb_data);
    return (gpi_cb_hdl)&m_read_only;
}

void gpi_deregister_callback(gpi_cb_hdl cb_hdl) {
    cb_hdl->cleanup_callback();
}

void *gpi_get_callback_data(gpi_cb_hdl cb_hdl) {
    return cb_hdl->get_user_data();
}

void gpi_sim_end(void) {vpi_control(vpiFinish, 0);}