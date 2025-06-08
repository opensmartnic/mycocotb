// 在这个文件里，整合原有的VpiImpl.cpp, VpiCbHdl.cpp, GpiCbHdl.cpp, embed.cpp, 
// gpi_embed.cpp

# include "VpiImpl.h"
#include <queue>
#include <Python.h>


extern "C" {
static VpiCbHdl *sim_init_cb;
#ifndef VPI_NO_QUEUE_SETIMMEDIATE_CALLBACKS
static std::deque<VpiCbHdl *> cb_queue;
#endif
static wchar_t progname[] = L"mycocotb";
static wchar_t *argv[] = {progname};

static int32_t handle_vpi_callback_(VpiCbHdl *cb_hdl) {
    gpi_to_user();

    if (!cb_hdl) {
        LOG_ERROR("VPI: Callback data corrupted: ABORTING");
        return -1;
    }

    gpi_cb_state_e old_state = cb_hdl->get_call_state();

    if (old_state == GPI_PRIMED) {
        cb_hdl->set_call_state(GPI_CALL);
        cb_hdl->run_callback();

        gpi_cb_state_e new_state = cb_hdl->get_call_state();

        /* We have re-primed in the handler */
        if (new_state != GPI_PRIMED)
            if (cb_hdl->cleanup_callback()) {
                delete cb_hdl;
            }

    } else {
        /* Issue #188: This is a work around for a modelsim */
        if (cb_hdl->cleanup_callback()) {
            delete cb_hdl;
        }
    }

    gpi_to_simulator();

    return 0;
}

// Main re-entry point for callbacks from simulator
int32_t handle_vpi_callback(p_cb_data cb_data) {
#ifdef VPI_NO_QUEUE_SETIMMEDIATE_CALLBACKS
    VpiCbHdl *cb_hdl = (VpiCbHdl *)cb_data->user_data;
    return handle_vpi_callback_(cb_hdl);
#else
    // 这个函数将由仿真器（如icarus）来触发，如果为了简单起见，可以像上面两行代码一样，
    // 直接调用handle_vpi_callback_(它做的工作是执行用户放置在cb_hdl里的gpi_function)。
    // 但这里使用的是后面更复杂的代码逻辑，主要是为了应对像vpiValueChanged这样的事件，
    // 我不清楚是否所有的仿真器都是一样的逻辑，但至少在icarus里，vpiValueChanged的回调
    // 函数里，如果对所监听的信号进行了赋值变更，那么会立即再触发一次vpiValueChanged,
    // 也就说，回调函数在没执行完之前，会又触发一次。这看起来也不会有太大的问题，但或许
    // 会有我们不想要的运行结果。所以下面的代码，会先把回调事件存放在一个队列里，等当前
    // 回调函数执行完之后，再从队列里取出事件，执行下一个的回调函数。
    // must push things into a queue because Icaurus (gh-4067), Xcelium
    // (gh-4013), and Questa (gh-4105) react to value changes on signals that
    // are set with vpiNoDelay immediately, and not after the current callback
    // has ended, causing re-entrancy.
    static bool reacting = false;
    VpiCbHdl *cb_hdl = (VpiCbHdl *)cb_data->user_data;
    if (reacting) {
        cb_queue.push_back(cb_hdl);
        return 0;
    }
    reacting = true;
    int32_t ret = handle_vpi_callback_(cb_hdl);
    while (!cb_queue.empty()) {
        handle_vpi_callback_(cb_queue.front());
        cb_queue.pop_front();
    }
    reacting = false;
    return ret;
#endif
}

static int get_interpreter_path(wchar_t *path, size_t path_size) {
    const char *path_c = getenv("PYGPI_PYTHON_BIN");
    if (!path_c) {
        // LCOV_EXCL_START
        LOG_ERROR("PYGPI_PYTHON_BIN variable not set. "
            "Can't initialize Python interpreter!\n");
        return -1;
        // LCOV_EXCL_STOP
    }

    auto path_temp = Py_DecodeLocale(path_c, NULL);
    if (path_temp == NULL) {
        // LCOV_EXCL_START
        LOG_ERROR(
            "Unable to set Python Program Name. "
            "Decoding error in Python executable path.");
        LOG_INFO("Python executable path: %s", path_c);
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(PyMem_RawFree(path_temp));

    wcsncpy(path, path_temp, path_size / sizeof(wchar_t));
    if (path[(path_size / sizeof(wchar_t)) - 1]) {
        // LCOV_EXCL_START
        LOG_ERROR(
            "Unable to set Python Program Name. Path to interpreter too long");
        LOG_INFO("Python executable path: %s", path_c);
        return -1;
        // LCOV_EXCL_STOP
    }

    return 0;
}

void _embed_init_python()
{
    static wchar_t interpreter_path[PATH_MAX], sys_executable[PATH_MAX];

    if (get_interpreter_path(interpreter_path, sizeof(interpreter_path))) {
        // LCOV_EXCL_START
        return;
        // LCOV_EXCL_STOP
    }
    LOG_INFO("Using Python interpreter at %ls", interpreter_path);

#if PY_VERSION_HEX >= 0x3080000
    /* Use the new Python Initialization Configuration from Python 3.8. */
    PyConfig config;
    PyStatus status;

    PyConfig_InitPythonConfig(&config);
    DEFER(PyConfig_Clear(&config));

    PyConfig_SetString(&config, &config.program_name, interpreter_path);

    status = PyConfig_SetArgv(&config, 1, argv);
    if (PyStatus_Exception(status)) {
        // LCOV_EXCL_START
        LOG_ERROR("Failed to set ARGV during the Python initialization");
        if (status.err_msg != NULL) {
            LOG_ERROR("\terror: %s", status.err_msg);
        }
        if (status.func != NULL) {
            LOG_ERROR("\tfunction: %s", status.func);
        }
        return;
        // LCOV_EXCL_STOP
    }

    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        // LCOV_EXCL_START
        LOG_ERROR("Failed to initialize Python");
        if (status.err_msg != NULL) {
            LOG_ERROR("\terror: %s", status.err_msg);
        }
        if (status.func != NULL) {
            LOG_ERROR("\tfunction: %s", status.func);
        }
        return;
        // LCOV_EXCL_STOP
    }
#else
    /* Use the old API. */
    Py_SetProgramName(interpreter_path);
    Py_Initialize();
    PySys_SetArgvEx(1, argv, 0);
#endif

    /* Sanity check: make sure sys.executable was initialized to
     * interpreter_path. */
    PyObject *sys_executable_obj = PySys_GetObject("executable");
    if (sys_executable_obj == NULL) {
        // LCOV_EXCL_START
        LOG_ERROR("Failed to load sys.executable");
        // LCOV_EXCL_STOP
    } else if (PyUnicode_AsWideChar(sys_executable_obj, sys_executable,
                                    sizeof(sys_executable)) == -1) {
        // LCOV_EXCL_START
        LOG_ERROR("Failed to convert sys.executable to wide string");
        // LCOV_EXCL_STOP
    } else if (wcscmp(interpreter_path, sys_executable) != 0) {
        // LCOV_EXCL_START
        LOG_ERROR("Unexpected sys.executable value (expected '%ls', got '%ls')",
                  interpreter_path, sys_executable);
        // LCOV_EXCL_STOP
    }
}

void gpi_entry_point() {
    _embed_init_python();
}

int _embed_sim_init(int argc, char const *const *_argv) {

    // Ensure that the current thread is ready to call the Python C API
    auto gstate = PyGILState_Ensure();
    DEFER(PyGILState_Release(gstate));

    to_python();
    DEFER(to_simulator());

    // 将当前目录(.)添加到sys.path中，以使得项目的mycocotb目录下的内容可以被import
    PyObject* path_obj = PyUnicode_DecodeFSDefault(".");
    PyObject* sys_path = PySys_GetObject("path");
    if (PyList_Insert(sys_path, 0, path_obj) == -1) {
        LOG_ERROR("Failed to insert current directory into sys.path");
    }
    Py_DECREF(path_obj);

    auto entry_utility_module = PyImport_ImportModule("mycocotb.entry");
    if (!entry_utility_module) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    DEFER(Py_DECREF(entry_utility_module));

    // Build argv for cocotb module
    auto argv_list = PyList_New(argc);
    if (argv_list == NULL) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    for (int i = 0; i < argc; i++) {
        // Decode, embedding non-decodable bytes using PEP-383. This can only
        // fail with MemoryError or similar.
        auto argv_item = PyUnicode_DecodeLocale(_argv[i], "surrogateescape");
        if (!argv_item) {
            // LCOV_EXCL_START
            PyErr_Print();
            return -1;
            // LCOV_EXCL_STOP
        }
        PyList_SetItem(argv_list, i, argv_item);
    }
    DEFER(Py_DECREF(argv_list))

    auto cocotb_retval =
    PyObject_CallMethod(entry_utility_module, "load_entry", "O", argv_list);
    if (!cocotb_retval) {
        // LCOV_EXCL_START
        PyErr_Print();
        return -1;
        // LCOV_EXCL_STOP
    }
    Py_DECREF(cocotb_retval);

    return 0;
}

static void register_initial_callback() {
    sim_init_cb = new VpiStartupCbHdl();
    sim_init_cb->arm_callback();
}

// 在这里定义两个vpi的钩子函数，他们将在仿真器启动时，由仿真器调用执行。
// 实际上只使用一个也行，但cocotb里分成了两个：一个负责初始化python环境，
// 如，获取python库路径、初始化配置等；一个负责执行实际的cocotb的基础python
// 代码，如启动事件循环等，并在启动完成后，跳入用户端的代码开始执行
void (*vlog_startup_routines[])() = {
    gpi_entry_point, register_initial_callback,
    nullptr
};

} // end of extern "C"


VpiCbHdl::VpiCbHdl() {
    vpi_time.high = 0;
    vpi_time.low = 0;
    vpi_time.type = vpiSimTime;

    cb_data.reason = 0;
    cb_data.cb_rtn = handle_vpi_callback;
    cb_data.obj = NULL;
    cb_data.time = &vpi_time;
    cb_data.value = NULL;
    cb_data.index = 0;
    cb_data.user_data = (char *)this;
}

int VpiCbHdl::arm_callback() {
    vpiHandle new_hdl = vpi_register_cb(&cb_data);
    if (!new_hdl) {
        LOG_ERROR("VPI: Failed to register callback\n");
        return -1;
    } else {
        m_state = GPI_PRIMED;
    }
    m_obj_hdl = new_hdl;
    return 0;
}

int VpiCbHdl::run_callback() {
    this->gpi_function(m_cb_data);
    return 0;
}

int VpiCbHdl::cleanup_callback() {
    if (m_state == GPI_FREE) return 0;

    /* If the one-time callback has not come back then
     * remove it, it is has then free it. The remove is done
     * internally */

    if (m_state == GPI_PRIMED) {
        if (!m_obj_hdl) {
            LOG_ERROR("VPI: passed a NULL pointer");
            return -1;
        }

        if (!(vpi_remove_cb(get_handle<vpiHandle>()))) {
            LOG_ERROR("VPI: unable to remove callback");
            return -1;
        }

    } 

    m_obj_hdl = NULL;
    m_state = GPI_FREE;

    return 0;
}

int VpiCbHdl::set_user_data(int (*_gpi_function)(void *), void *data) {
    if (!_gpi_function) {
        vpi_printf("gpi_function to set_user_data is NULL");
    }
    this->gpi_function = _gpi_function;
    this->m_cb_data = data;
    return 0;
}

void VpiCbHdl::set_call_state(gpi_cb_state_e new_state) { m_state = new_state; }
gpi_cb_state_e VpiCbHdl::get_call_state() { return m_state; }


VpiStartupCbHdl::VpiStartupCbHdl() {
    cb_data.reason = cbStartOfSimulation;
}

int VpiStartupCbHdl::run_callback() {
    s_vpi_vlog_info info;

    if (!vpi_get_vlog_info(&info)) {
        LOG_WARN("Unable to get argv and argc from simulator");
        info.argc = 0;
        info.argv = nullptr;
    }

    _embed_sim_init(info.argc, info.argv);

    return 0;
}

VpiTimedCbHdl::VpiTimedCbHdl(uint64_t time) {
    vpi_time.high = (uint32_t)(time >> 32);
    vpi_time.low = (uint32_t)(time);
    vpi_time.type = vpiSimTime;

    cb_data.reason = cbAfterDelay;
}

VpiValueCbHdl::VpiValueCbHdl(GpiImplInterface *impl, VpiSignalObjHdl *sig,
                             gpi_edge_e edge) {
    vpi_time.type = vpiSuppressTime;
    m_vpi_value.format = vpiIntVal;

    cb_data.reason = cbValueChange;
    cb_data.time = &vpi_time;
    cb_data.value = &m_vpi_value;
    m_signal = sig;
    cb_data.obj = sig->get_handle<vpiHandle>();
    switch (edge) {
        case GPI_RISING: {
            required_value = "1";
            break;
        }
        case GPI_FALLING: {
            required_value = "0";
            break;
        }
        case GPI_VALUE_CHANGE: {
            required_value = "X";
            break;
        }
    }
}

int VpiValueCbHdl::run_callback() {
    std::string current_value;
    bool pass = false;

    if (required_value == "X")
        pass = true;
    else {
        current_value = m_signal->get_signal_value_binstr();
        if (current_value == required_value) pass = true;
    }

    if (pass) {
        this->gpi_function(m_cb_data);
    } else {
        cleanup_callback();
        arm_callback();
    }

    return 0;
}


int VpiValueCbHdl::cleanup_callback() {
    if (m_state == GPI_FREE) return 0;

    /* This is a recurring callback so just remove when
     * not wanted */
    if (!(vpi_remove_cb(get_handle<vpiHandle>()))) {
        LOG_ERROR("VPI: unable to remove callback");
        return -1;
    }

    m_obj_hdl = NULL;
    m_state = GPI_FREE;
    return 0;
}

VpiNextPhaseCbHdl::VpiNextPhaseCbHdl() {
    cb_data.reason = cbNextSimTime;
}

VpiReadWriteCbHdl::VpiReadWriteCbHdl() {
    cb_data.reason = cbReadWriteSynch;
}

VpiReadOnlyCbHdl::VpiReadOnlyCbHdl() {
    cb_data.reason = cbReadOnlySynch;
}