/******************************************************************************
 * Copyright (c) 2013, 2018 Potential Ventures Ltd
 * Copyright (c) 2013 SolarFlare Communications Inc
 * All rights reserved.
*/

#include <assert.h>

#include <stdexcept>
#include <algorithm>

#include "VpiImpl.h"
#include "gpi_priv.h"

int VpiSignalObjHdl::initialise(const std::string &name,
                                const std::string &fq_name) {
    int32_t type = vpi_get(vpiType, GpiObjHdl::get_handle<vpiHandle>());
    if ((vpiIntegerVar == type)) {
        m_num_elems = 1;
    } else {
        m_num_elems = vpi_get(vpiSize, GpiObjHdl::get_handle<vpiHandle>());

        if (GpiObjHdl::get_type() == GPI_STRING || type == vpiConstant ||
            type == vpiParameter) {
            m_indexable = false;  // Don't want to iterate over indices
            m_range_left = 0;
            m_range_right = m_num_elems - 1;
        } else if (GpiObjHdl::get_type() == GPI_LOGIC ||
                   GpiObjHdl::get_type() == GPI_LOGIC_ARRAY) {
            vpiHandle hdl = GpiObjHdl::get_handle<vpiHandle>();
            m_indexable = vpi_get(vpiVector, hdl);

            if (m_indexable) {
                s_vpi_value val;
                vpiHandle iter;

                val.format = vpiIntVal;
                const int vpiRange = 115;
                iter = vpi_iterate(vpiRange, hdl);

                /* Only ever need the first "range" */
                if (iter != NULL) {
                    vpiHandle rangeHdl = vpi_scan(iter);

                    vpi_free_object(iter);

                    if (rangeHdl != NULL) {
                        vpi_get_value(vpi_handle(vpiLeftRange, rangeHdl), &val);
                        check_vpi_error();
                        m_range_left = val.value.integer;

                        vpi_get_value(vpi_handle(vpiRightRange, rangeHdl),
                                      &val);
                        check_vpi_error();
                        m_range_right = val.value.integer;
                    } else {
                        LOG_ERROR(
                            "VPI: Unable to get range for %s of type %s (%d)",
                            name.c_str(), vpi_get_str(vpiType, hdl), type);
                        return -1;
                    }
                } else {
                    vpiHandle leftRange = vpi_handle(vpiLeftRange, hdl);
                    check_vpi_error();
                    vpiHandle rightRange = vpi_handle(vpiRightRange, hdl);
                    check_vpi_error();

                    if (leftRange != NULL and rightRange != NULL) {
                        vpi_get_value(leftRange, &val);
                        m_range_left = val.value.integer;

                        vpi_get_value(rightRange, &val);
                        m_range_right = val.value.integer;
                    } else {
                        LOG_WARN(
                            "VPI: Cannot discover range bounds, guessing based "
                            "on elements");
                        m_range_left = 0;
                        m_range_right = m_num_elems - 1;
                    }
                }

                LOG_DEBUG(
                    "VPI: Indexable object initialized with range [%d:%d] and "
                    "length >%d<",
                    m_range_left, m_range_right, m_num_elems);
            } else {
                m_range_left = 0;
                m_range_right = m_num_elems - 1;
            }
        }
    }
    m_range_dir = m_range_left > m_range_right ? GPI_RANGE_DOWN : GPI_RANGE_UP;
    LOG_DEBUG("VPI: %s initialized with %d elements", name.c_str(),
              m_num_elems);
    return GpiObjHdl::initialise(name, fq_name);
}

const char *VpiSignalObjHdl::get_signal_value_binstr() {
    s_vpi_value value_s = {vpiBinStrVal, {NULL}};

    vpi_get_value(GpiObjHdl::get_handle<vpiHandle>(), &value_s);
    check_vpi_error();

    return value_s.value.str;
}

// Value related functions
int VpiSignalObjHdl::set_signal_value(int32_t value, gpi_set_action_t action) {
    s_vpi_value value_s;

    value_s.value.integer = static_cast<PLI_INT32>(value);
    value_s.format = vpiIntVal;

    return set_signal_value(value_s, action);
}

int VpiSignalObjHdl::set_signal_value(double value, gpi_set_action_t action) {
    s_vpi_value value_s;

    value_s.value.real = value;
    value_s.format = vpiRealVal;

    return set_signal_value(value_s, action);
}

int VpiSignalObjHdl::set_signal_value_binstr(std::string &value,
                                             gpi_set_action_t action) {
    s_vpi_value value_s;

    std::vector<char> writable(value.begin(), value.end());
    writable.push_back('\0');

    value_s.value.str = &writable[0];
    value_s.format = vpiBinStrVal;

    return set_signal_value(value_s, action);
}


int VpiSignalObjHdl::set_signal_value(s_vpi_value value_s,
                                      gpi_set_action_t action) {
    PLI_INT32 vpi_put_flag = -1;
    s_vpi_time vpi_time_s;

    vpi_time_s.type = vpiSimTime;
    vpi_time_s.high = 0;
    vpi_time_s.low = 0;

    switch (action) {
        case GPI_DEPOSIT:
#if defined(MODELSIM) || defined(IUS)
            // Xcelium and Questa do not like setting string variables using
            // vpiInertialDelay.
            if (vpiStringVar ==
                vpi_get(vpiType, GpiObjHdl::get_handle<vpiHandle>())) {
                vpi_put_flag = vpiNoDelay;
            } else {
                vpi_put_flag = vpiInertialDelay;
            }
#else
            vpi_put_flag = vpiInertialDelay;
#endif
            break;
        case GPI_FORCE:
            vpi_put_flag = vpiForceFlag;
            break;
        case GPI_RELEASE:
            // Best to pass its current value to the sim when releasing
            vpi_get_value(GpiObjHdl::get_handle<vpiHandle>(), &value_s);
            vpi_put_flag = vpiReleaseFlag;
            break;
        case GPI_NO_DELAY:
            vpi_put_flag = vpiNoDelay;
            break;
        default:
            assert(0);
    }

    if (vpi_put_flag == vpiNoDelay) {
        vpi_put_value(GpiObjHdl::get_handle<vpiHandle>(), &value_s, NULL,
                      vpiNoDelay);
    } else {
        vpi_put_value(GpiObjHdl::get_handle<vpiHandle>(), &value_s, &vpi_time_s,
                      vpi_put_flag);
    }

    check_vpi_error();

    return 0;
}

VpiCbHdl *VpiSignalObjHdl::register_value_change_callback(
    gpi_edge_e edge, int (*function)(void *), void *cb_data) {
    VpiValueCbHdl *cb = new VpiValueCbHdl(this->m_impl, this, edge);
    cb->set_user_data(function, cb_data);
    if (cb->arm_callback()) {
        return NULL;
    }
    return cb;
}

int VpiArrayObjHdl::initialise(const std::string &name,
                              const std::string &fq_name) {
   vpiHandle hdl = GpiObjHdl::get_handle<vpiHandle>();

   m_indexable = true;

   int range_idx = 0;

   /* Need to determine if this is a pseudo-handle to be able to select the
    * correct range */
   std::string hdl_name = vpi_get_str(vpiName, hdl);

   /* Removing the hdl_name from the name will leave the pseudo-indices */
   if (hdl_name.length() < name.length()) {
       // get the last index of hdl_name in name
       std::size_t idx_str = name.rfind(hdl_name);
       if (idx_str == std::string::npos) {
           LOG_ERROR("Unable to find name %s in %s", hdl_name.c_str(),
                     name.c_str());
           return -1;
       }
       // count occurences of [
       auto start =
           name.begin() + static_cast<std::string::difference_type>(idx_str);
       range_idx = static_cast<int>(std::count(start, name.end(), '['));
   }

   /* After determining the range_idx, get the range and set the limits */
   const int vpiRange = 115;
   vpiHandle iter = vpi_iterate(vpiRange, hdl);
   vpiHandle rangeHdl;

   if (iter != NULL) {
       rangeHdl = vpi_scan(iter);

       for (int i = 0; i < range_idx; ++i) {
           rangeHdl = vpi_scan(iter);
           if (rangeHdl == NULL) {
               break;
           }
       }
       if (rangeHdl == NULL) {
           LOG_ERROR("Unable to get range for indexable array");
           return -1;
       }
       vpi_free_object(iter);  // Need to free iterator since exited early
   } else if (range_idx == 0) {
       rangeHdl = hdl;
   } else {
       LOG_ERROR("Unable to get range for indexable array or memory");
       return -1;
   }

   s_vpi_value val;
   val.format = vpiIntVal;
   vpi_get_value(vpi_handle(vpiLeftRange, rangeHdl), &val);
   check_vpi_error();
   m_range_left = val.value.integer;

   vpi_get_value(vpi_handle(vpiRightRange, rangeHdl), &val);
   check_vpi_error();
   m_range_right = val.value.integer;

   /* vpiSize will return a size that is incorrect for multi-dimensional arrays
    * so use the range to calculate the m_num_elems.
    *
    *    For example:
    *       wire [7:0] sig_t4 [0:3][7:4]
    *
    *    The size of "sig_t4" will be reported as 16 through the vpi interface.
    */
   if (m_range_left > m_range_right) {
       m_num_elems = m_range_left - m_range_right + 1;
       m_range_dir = GPI_RANGE_DOWN;
   } else {
       m_num_elems = m_range_right - m_range_left + 1;
       m_range_dir = GPI_RANGE_UP;
   }

   return GpiObjHdl::initialise(name, fq_name);
}

