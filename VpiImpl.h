// 在COCOTB里，支持多种编程语言和仿真器，所以需要支持多种编程接口（VPI、VHPI、FLI）
// 而为了统一这些接口，COCOTB又抽象了一层称为GPI，由GPI根据具体的仿真入口来决定是
// 调用VPI还是VHPI还是FLI。
// 在我们这里只支持VPI，所以有些代码进行了简化，如果不是特别复杂的，就直接使用VPI函数。
// 但也有些函数是从原有代码直接拷贝过来的，它们会直接保留"gpi_**"的前缀。
// 所以在这里，你大概会看到vpi**, gpi**的混用。大致可以认为他们是一个东西
#ifndef COCOTB_VPI_IMPL_H_
#define COCOTB_VPI_IMPL_H_

#include <stdlib.h>
#include <vpi_user.h>
#include <unistd.h>
#include "gpi_priv.h"

#define LOG_ERROR(format, ...) vpi_printf(format "\n", ##__VA_ARGS__)
#define LOG_WARN(format, ...) vpi_printf(format "\n", ##__VA_ARGS__)
#define LOG_INFO(format, ...) vpi_printf(format "\n", ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) vpi_printf(format "\n", ##__VA_ARGS__)
#define LOG_TRACE(format, ...) vpi_printf(format "\n", ##__VA_ARGS__)
#define LOG_FATAL(format, ...) vpi_printf(format "\n", ##__VA_ARGS__)

// #define PATH_MAX 256

#define to_python() do { LOG_TRACE("Returning to Python"); } while (0)
#define to_simulator() do { LOG_TRACE("Returning to simulator"); } while (0)

template <typename F>
class Deferable {
  public:
    constexpr Deferable(F f) : f_(f) {};
    ~Deferable() { f_(); }

  private:
    F f_;
};

template <typename F>
constexpr Deferable<F> make_deferable(F f) {
    return Deferable<F>(f);
}

#define DEFER1(a, b) a##b
#define DEFER0(a, b) DEFER1(a, b)
#define DEFER(statement) \
    auto DEFER0(_defer, __COUNTER__) = make_deferable([&]() { statement; });


class VpiCbHdl {
  public:
    VpiCbHdl();

    virtual int arm_callback();
    virtual int run_callback();
    virtual int cleanup_callback();
    void set_call_state(gpi_cb_state_e new_state);
    gpi_cb_state_e get_call_state();

    int set_user_data(int (*function)(void *), void *cb_data);
    void *get_user_data() noexcept { return m_cb_data; };

    template <typename T> T get_handle() const {
        return static_cast<T>(m_obj_hdl);
    }

  protected:
    s_cb_data cb_data;
    s_vpi_time vpi_time;
    gpi_cb_state_e m_state = GPI_FREE;  // GPI state of the callback through its cycle
    int (*gpi_function)(void *) = nullptr;  // GPI function to callback
    void *m_cb_data = nullptr;  // GPI data supplied to "gpi_function"

    void *m_obj_hdl;  // 在这里存放vpi_register_cb后的返回值，调用vpi_remove_cb时要用
};

class VpiStartupCbHdl : public VpiCbHdl
{  
  public:
    VpiStartupCbHdl();
    int run_callback() override;
};

class VpiTimedCbHdl : public VpiCbHdl {
  public:
    VpiTimedCbHdl(uint64_t time);
};

class VpiValueCbHdl : public VpiCbHdl {
  public:
    VpiValueCbHdl(GpiImplInterface *impl, VpiSignalObjHdl *sig,
                  gpi_edge_e edge);
    int run_callback() override;
    int cleanup_callback() override;

  private:
    s_vpi_value m_vpi_value;
    GpiSignalObjHdl *m_signal;
    std::string required_value;
};

class VpiNextPhaseCbHdl : public VpiCbHdl {
  public:
    VpiNextPhaseCbHdl();
};

class VpiReadWriteCbHdl : public VpiCbHdl {
  public:
    VpiReadWriteCbHdl();
};

class VpiReadOnlyCbHdl : public VpiCbHdl {
  public:
    VpiReadOnlyCbHdl();
};

#define gpi_to_user()  do { vpi_printf("Passing control to GPI user\n"); } while (0)
#define gpi_to_simulator() do { vpi_printf("Return control to simulator\n"); } while (0)


#endif