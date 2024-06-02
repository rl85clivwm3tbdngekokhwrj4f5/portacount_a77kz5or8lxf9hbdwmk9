#include <GL/freeglut.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <semaphore.h>
#include <limits>
#include <vector>
#include <utility>
#include <tuple>
#include <atomic>

namespace
{
    #define checkError(ret, expected, msg) checkErrorHelper( (ret), (expected), (msg), __LINE__)
    template <class T>
    static inline void checkErrorHelper(const T ret, const T expected, const char *const msg, const int line)
    {
        if(ret != expected)
        {
            fprintf(stderr, "%s at line %d\n", msg, line);
            perror("");
            exit(1);
        }
    }

    #define checkError2(ret, error, msg) checkError2Helper( (ret), (error), (msg), __LINE__)
    template <class T>
    static inline void checkError2Helper(const T ret, const T error, const char *const msg, const int line)
    {
        if(ret == error)
        {
            fprintf(stderr, "%s at line %d\n", msg, line);
            perror("");
            exit(1);
        }
    }

    #define checkError3(ret, bound, msg) checkError3Helper( (ret), (bound), (msg), __LINE__)
    template <class T>
    static inline void checkError3Helper(const T ret, const T bound, const char *const msg, const int line)
    {
        if(ret < 0 || ret >= bound)
        {
            fprintf(stderr, "%s at line %d\n", msg, line);
            perror("");
            exit(1);
        }
    }

    #define assertWithMsg(cond, msg) assertWithMsgHelper( (cond), (#cond), (msg), __LINE__)
    static inline void assertWithMsgHelper(const bool condition, const char *const cond_str, const char *const msg, const int line)
    {
        if(condition == false)
        {
            fprintf(stderr, "%s\n", msg);
            fprintf(stderr, "Assertion %s failed at line %d\n", cond_str, line);
            exit(1);
        }
    }

    static inline void writeFully(const int fd, const void *const buf, const size_t count)
    {
        ssize_t ret;
        size_t remaining = count;
        ssize_t offset = 0;
        const char *const buffer = static_cast<const char *>(buf);

        do
        {
            ret = write(fd, buffer + offset, remaining);
            checkError2(ret, -1L, "write error");
            remaining = remaining - static_cast<size_t>(ret);
            offset = offset + ret;
        }while(remaining > 0);
    }

    struct ViewportDimension
    {
        int window_width;
        int window_height;
    };
    static ViewportDimension window = {.window_width = 958, .window_height = 958};

    struct OrthographicProjectionDimension
    {
        const double LEFT_BOUND;
        const double RIGHT_BOUND;
        const double BOTTOM_BOUND;
        const double TOP_BOUND;
    };
    static constexpr const OrthographicProjectionDimension PROJECTION = {.LEFT_BOUND = 0.0, .RIGHT_BOUND = 10.0, .BOTTOM_BOUND = 0.0, .TOP_BOUND = 10.0};

    static constexpr const int SAMPLE_COUNT = 16;

    struct FileDescriptors
    {
        int serial_fd;
        int outfile_fd;
    };
    static FileDescriptors fds;

    enum class ModeType
    {
        COUNT_MODE,
        FIT_TEST_MODE
    };
    static ModeType mode = ModeType::COUNT_MODE;

    struct CountModeData
    {
        double count_mode_x_axis_max;
        double count_array_max;
        double count_array_min;
        std::vector<double> count_array;
    };
    static CountModeData count_mode_data = {
        .count_mode_x_axis_max = 18.0, 
        .count_array_max = -std::numeric_limits<double>::max(),
        .count_array_min = std::numeric_limits<double>::max(),
        .count_array = std::vector<double>()
    };

    struct FitTestModeData
    {
        double fit_test_mode_x_axis_max;
        double sample_array_max;
        double sample_array_min;
        double ambient_array_max;
        double ambient_array_min;
        double fit_factor_array_max;
        double fit_factor_array_min;
        std::vector<double> sample_array, ambient_array, fit_factor_array;
    };
    static FitTestModeData fit_test_mode_data = {
        .fit_test_mode_x_axis_max = 18.0,
        .sample_array_max = -std::numeric_limits<double>::max(),
        .sample_array_min = std::numeric_limits<double>::max(),
        .ambient_array_max = -std::numeric_limits<double>::max(),
        .ambient_array_min = std::numeric_limits<double>::max(),
        .fit_factor_array_max = -std::numeric_limits<double>::max(),
        .fit_factor_array_min = std::numeric_limits<double>::max(),
        .sample_array = std::vector<double>(),
        .ambient_array = std::vector<double>(),
        .fit_factor_array = std::vector<double>()
    };

    struct Color
    {
        double R_value;
        double G_value;
        double B_value;
    };
    static Color color = {.R_value = 1.0, .G_value = 0.0, .B_value = 0.0};
    
    struct SharedMemoryBuffer
    {
        std::atomic<bool> initialized;
        std::atomic<bool> quit;
        bool valid;
        ModeType mode;
        struct CountMode
        {
            bool y_axis_valid;
            double y_axis_min;
            double y_axis_max;
        };
        struct FitTestMode
        {
            bool sample_y_axis_valid;
            bool ambient_y_axis_valid;
            bool fit_factor_y_axis_valid;
            double sample_y_axis_min;
            double sample_y_axis_max;
            double ambient_y_axis_min;
            double ambient_y_axis_max;
            double fit_factor_y_axis_min;
            double fit_factor_y_axis_max;
        };
        CountMode count_mode;
        FitTestMode fit_test_mode;
    };
    static sem_t **semaphore_ptrs;
    static SharedMemoryBuffer **shared_memory_ptrs;

    struct InstanceData
    {
        unsigned int total_instances;
        unsigned int instance_index;
    };
    static InstanceData instance;

    static constexpr const char *const shared_memory_prefix = "/Portacount_vyjcicyipdclbkthgcrppallfevgbjkk";

    static void reshape(const int width, const int height) 
    {
        window.window_width  = width;
        window.window_height = height; 

        // update viewport
        glViewport(0, 0, width, height);  
    }

    static void keyboard_func(const unsigned char key, const int x, const int y)
    {
        (void)x;
        (void)y;

        switch(key)
        {
            case 'q':
            case 'Q':
                glutLeaveMainLoop();
                break;

            case 'c':
            case 'C':
                mode = ModeType::COUNT_MODE;
                // signal redraw
                glutPostRedisplay();
                break;

            case 'f':
            case 'F':
                mode = ModeType::FIT_TEST_MODE;
                // signal redraw
                glutPostRedisplay();
                break;

            case 'x':
            case 'X':
                if(mode == ModeType::COUNT_MODE)
                {
                    count_mode_data.count_array.clear();
                    count_mode_data.count_array.shrink_to_fit();
                    count_mode_data.count_array.reserve(20);
                    count_mode_data.count_mode_x_axis_max = 18.0;
                    count_mode_data.count_array_max = -std::numeric_limits<double>::max();
                    count_mode_data.count_array_min = std::numeric_limits<double>::max();
                }
                else if(mode == ModeType::FIT_TEST_MODE)
                {
                    fit_test_mode_data.sample_array.clear();
                    fit_test_mode_data.sample_array.shrink_to_fit();
                    fit_test_mode_data.sample_array.reserve(20);
                    fit_test_mode_data.ambient_array.clear();
                    fit_test_mode_data.ambient_array.shrink_to_fit();
                    fit_test_mode_data.ambient_array.reserve(20);
                    fit_test_mode_data.fit_factor_array.clear();
                    fit_test_mode_data.fit_factor_array.shrink_to_fit();
                    fit_test_mode_data.fit_factor_array.reserve(20);
                    fit_test_mode_data.fit_test_mode_x_axis_max = 18.0;
                    fit_test_mode_data.sample_array_max = -std::numeric_limits<double>::max();
                    fit_test_mode_data.sample_array_min = std::numeric_limits<double>::max();
                    fit_test_mode_data.ambient_array_max = -std::numeric_limits<double>::max();
                    fit_test_mode_data.ambient_array_min = std::numeric_limits<double>::max();
                    fit_test_mode_data.fit_factor_array_max = -std::numeric_limits<double>::max();
                    fit_test_mode_data.fit_factor_array_min = std::numeric_limits<double>::max();
                }
                // signal redraw
                glutPostRedisplay();
                break;
        }
    }

    static void draw_vertical_linear_lines(const double x_begin, const double x_inc,
        const double y_begin, const double y_end, const unsigned int count)
    {
        double vertex_x = x_begin;
        const double vertex1_y = y_begin;
        const double vertex2_y = y_end;

        glBegin(GL_LINES);
        for(unsigned int i = 0; i < count; i++)
        {
            glVertex3d(vertex_x, vertex1_y, 0.0);
            glVertex3d(vertex_x, vertex2_y, 0.0);
            vertex_x += x_inc;
        }
        glEnd();
    }

    static void draw_horizontal_log10_lines(const double x_begin, const double x_end,
        const double y_begin, const double y_inc, const unsigned int count)
    {
        const double vertex1_x = x_begin;
        const double vertex2_x = x_end;
        double vertex_y = y_begin;

        glBegin(GL_LINES);
        for(unsigned int i = 0; i < count; i++)
        {
            glVertex3d(vertex1_x, vertex_y, 0.0);
            glVertex3d(vertex2_x, vertex_y, 0.0);
            for(unsigned int j = 2; j <= 9; j++)
            {
                const double y_minor = static_cast<double>(j);
                double log_y = log10(y_minor);
                log_y *= y_inc;
                glVertex3d(vertex1_x + 0.1, vertex_y + log_y, 0.0);
                glVertex3d(vertex2_x, vertex_y + log_y, 0.0);
            }
            vertex_y += y_inc;            
        }
        glVertex3d(vertex1_x, vertex_y, 0.0);
        glVertex3d(vertex2_x, vertex_y, 0.0);
        glEnd();
    }

    static void draw_horizontal_string(const char *const str, const double scale, const double translate_x, const double translate_y)
    {
        glPushMatrix();
        glTranslated(translate_x, translate_y, 0.0);
        glScaled(scale, scale, scale);
        glutStrokeString(GLUT_STROKE_MONO_ROMAN, reinterpret_cast<const unsigned char *>(str) );
        glPopMatrix();
    }

    static void draw_vertical_string(const char *const str, const double scale, const double translate_x, const double translate_y)
    {
        glPushMatrix();
        glTranslated(translate_x, translate_y, 0.0);
        glRotated(90.0, 0.0, 0.0, 1.0);
        glScaled(scale, scale, scale);
        glutStrokeString(GLUT_STROKE_MONO_ROMAN, reinterpret_cast<const unsigned char *>(str) );
        glPopMatrix();
    }

    static inline std::tuple<double, double, bool> compute_y_axis(const double min, const double max, const double default_min, const double default_max)
    {
        double y_axis_min = floor(min);
        double y_axis_max = ceil(max);
        bool default_used = false;
        if(y_axis_max < y_axis_min)
        {
            y_axis_min = default_min;
            y_axis_max = default_max;
            default_used = true;
        }
        if(y_axis_max - y_axis_min < 1.0)
        {
            y_axis_max = y_axis_min + 1.0;
        }
        return std::make_tuple(y_axis_min, y_axis_max, default_used);
    }

    static void display(void) 
    {
        // check if there have been any openGL problems
        const GLenum errCode = glGetError();
        if(errCode != GL_NO_ERROR) 
        {
            const GLubyte* errString = gluErrorString(errCode);
            fprintf(stderr, "OpenGL error: %s\n", errString);
        }

        // clear the frame buffer
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // set the orthographic projection matrix
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(PROJECTION.LEFT_BOUND, PROJECTION.RIGHT_BOUND, PROJECTION.BOTTOM_BOUND, PROJECTION.TOP_BOUND);

        // set up the camera transformation
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glColor3d(0.0, 0.0, 0.0);

        constexpr const double axis_x_begin = 0.75;
        constexpr const double axis_x_end = 10.0;
        constexpr const double x_axis_inc = 0.5;
        constexpr const unsigned int x_axis_count = 19;
        constexpr const double x_axis_count_divisor = static_cast<double>(x_axis_count) - 1.0;

        if(mode == ModeType::COUNT_MODE)
        {
            constexpr const double axis_y_begin = 0.5;
            constexpr const double axis_y_end = 10.0;

            // draw x-axis
            draw_vertical_linear_lines(axis_x_begin, x_axis_inc, axis_y_begin, axis_y_end, x_axis_count);

            // draw y-axis
            double y_axis_min, y_axis_max;
            bool default_y_axis;
            std::tie(y_axis_min, y_axis_max, default_y_axis) = compute_y_axis(count_mode_data.count_array_min, count_mode_data.count_array_max, -3.0, 5.0);

            // synchronize y-axis scales across multiple process instances
            checkError(sem_wait(semaphore_ptrs[instance.instance_index]), 0, "sem_wait error");
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if(default_y_axis == false)
            {
                shared_memory_ptrs[instance.instance_index]->mode = ModeType::COUNT_MODE;
                shared_memory_ptrs[instance.instance_index]->count_mode.y_axis_min = y_axis_min;            
                shared_memory_ptrs[instance.instance_index]->count_mode.y_axis_max = y_axis_max;
                shared_memory_ptrs[instance.instance_index]->count_mode.y_axis_valid = true;            
                shared_memory_ptrs[instance.instance_index]->valid = true;
            }
            std::atomic_thread_fence(std::memory_order_seq_cst);
            checkError(sem_post(semaphore_ptrs[instance.instance_index]), 0, "sem_post error");

            for(unsigned int i = 0; i < instance.total_instances; i++)
            {
                if(i == instance.instance_index)
                {
                    continue;
                }
                checkError(sem_wait(semaphore_ptrs[i]), 0, "sem_wait error");
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if(shared_memory_ptrs[i]->valid == true && shared_memory_ptrs[i]->mode == ModeType::COUNT_MODE)
                {
                    if(shared_memory_ptrs[i]->count_mode.y_axis_valid == true)
                    {
                        if(shared_memory_ptrs[i]->count_mode.y_axis_min < y_axis_min)
                        {
                            y_axis_min = shared_memory_ptrs[i]->count_mode.y_axis_min;
                        }
                        if(shared_memory_ptrs[i]->count_mode.y_axis_max > y_axis_max)
                        {
                            y_axis_max = shared_memory_ptrs[i]->count_mode.y_axis_max;
                        }
                    }            
                }
                std::atomic_thread_fence(std::memory_order_seq_cst);
                checkError(sem_post(semaphore_ptrs[i]), 0, "sem_post error");
            }

            const unsigned int y_axis_range = static_cast<unsigned int>(rint(y_axis_max - y_axis_min));
            const double y_axis_inc = 9.3 / static_cast<double>(y_axis_range);
            draw_horizontal_log10_lines(axis_x_begin, axis_x_end, axis_y_begin, y_axis_inc, y_axis_range);

            // draw x-axis label
            draw_horizontal_string("Time", 0.002, 4.5, 0.05);

            // draw x-axis ticks
            char buf[32];
            for(unsigned int i = 0; i < x_axis_count; i+=2)
            {
                memset(buf, 0, sizeof(buf) );
                const double temp = rint(static_cast<double>(i) / x_axis_count_divisor * count_mode_data.count_mode_x_axis_max);
                static_assert(static_cast<int>(sizeof(buf) - 1) == sizeof(buf) - 1, "Size overflow");
                checkError3(snprintf(buf, sizeof(buf) - 1, "%u", static_cast<unsigned int>(temp ) ), static_cast<int>(sizeof(buf) - 1), "snprintf error");
                draw_horizontal_string(buf, 0.001, axis_x_begin - 0.05 + x_axis_inc * static_cast<double>(i), 0.31);
            }        

            // draw y-axis label
            draw_vertical_string("Count", 0.002, 0.25, 4.5);

            // draw y-axis ticks
            for(unsigned int i = 0; i <= y_axis_range; i++)
            {
                memset(buf, 0, sizeof(buf) );
                static_assert(static_cast<int>(sizeof(buf) - 1) == sizeof(buf) - 1, "Size overflow");
                checkError3(snprintf(buf, sizeof(buf) - 1, "1e%+d", static_cast<int>(y_axis_min) + static_cast<int>(i) ), static_cast<int>(sizeof(buf) - 1), "snprintf error");
                draw_horizontal_string(buf, 0.001, 0.3, axis_y_begin + y_axis_inc * static_cast<double>(i) );
            }        

            // draw data points
            glColor3d(color.R_value, color.G_value, color.B_value);
            glPointSize(8.0);
            glBegin(GL_POINTS);
            const double multiplier = 1.0 / count_mode_data.count_mode_x_axis_max;
            for(size_t i = 0, size = count_mode_data.count_array.size(); i < size; i++)
            {
                const double x_coord = static_cast<double>(i) * multiplier * 9.0 + axis_x_begin;
                const double y_coord = (count_mode_data.count_array[i] - y_axis_min) * y_axis_inc + axis_y_begin;
                glVertex3d(x_coord, y_coord, 0.1);
            }
            glEnd();
        }
        else if(mode == ModeType::FIT_TEST_MODE)
        {
            constexpr const double axis_y_jump = 3.3;

            constexpr const double ambient_axis_y_begin = 0.5;
            constexpr const double ambient_axis_y_end = 3.3;
            constexpr const double sample_axis_y_begin = 0.5 + axis_y_jump;
            constexpr const double sample_axis_y_end = 3.3 + axis_y_jump;
            constexpr const double fit_factor_axis_y_begin = 0.5 + axis_y_jump * 2.0;
            constexpr const double fit_factor_axis_y_end = 3.3 + axis_y_jump * 2.0;

            // draw x-axis
            draw_vertical_linear_lines(axis_x_begin, x_axis_inc, ambient_axis_y_begin, ambient_axis_y_end, x_axis_count);
            draw_vertical_linear_lines(axis_x_begin, x_axis_inc, sample_axis_y_begin, sample_axis_y_end, x_axis_count);
            draw_vertical_linear_lines(axis_x_begin, x_axis_inc, fit_factor_axis_y_begin, fit_factor_axis_y_end, x_axis_count);

            // draw y-axis
            double ambient_y_axis_min, ambient_y_axis_max;
            bool ambient_default_y_axis;
            std::tie(ambient_y_axis_min, ambient_y_axis_max, ambient_default_y_axis) = compute_y_axis(fit_test_mode_data.ambient_array_min, fit_test_mode_data.ambient_array_max, 3.0, 6.0);

            double sample_y_axis_min, sample_y_axis_max;
            bool sample_default_y_axis;
            std::tie(sample_y_axis_min, sample_y_axis_max, sample_default_y_axis) = compute_y_axis(fit_test_mode_data.sample_array_min, fit_test_mode_data.sample_array_max, -1.0, 3.0);

            double fit_factor_y_axis_min, fit_factor_y_axis_max;
            bool fit_factor_default_y_axis;
            std::tie(fit_factor_y_axis_min, fit_factor_y_axis_max, fit_factor_default_y_axis) = compute_y_axis(fit_test_mode_data.fit_factor_array_min, fit_test_mode_data.fit_factor_array_max, 0.0, 3.0);

            // synchronize y-axis scales across multiple process instances
            checkError(sem_wait(semaphore_ptrs[instance.instance_index]), 0, "sem_wait error");
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if(ambient_default_y_axis == false)
            {
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.ambient_y_axis_min = ambient_y_axis_min;            
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.ambient_y_axis_max = ambient_y_axis_max;
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.ambient_y_axis_valid = true;
            }
            if(sample_default_y_axis == false)
            {
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.sample_y_axis_min = sample_y_axis_min;            
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.sample_y_axis_max = sample_y_axis_max;
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.sample_y_axis_valid = true;
            }
            if(fit_factor_default_y_axis == false)
            {
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.fit_factor_y_axis_min = fit_factor_y_axis_min;            
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.fit_factor_y_axis_max = fit_factor_y_axis_max;
                shared_memory_ptrs[instance.instance_index]->fit_test_mode.fit_factor_y_axis_valid = true;
            }
            if(ambient_default_y_axis == false || sample_default_y_axis == false || fit_factor_default_y_axis == false)
            {
                shared_memory_ptrs[instance.instance_index]->mode = ModeType::FIT_TEST_MODE;
                shared_memory_ptrs[instance.instance_index]->valid = true;
            }
            std::atomic_thread_fence(std::memory_order_seq_cst);
            checkError(sem_post(semaphore_ptrs[instance.instance_index]), 0, "sem_post error");

            for(unsigned int i = 0; i < instance.total_instances; i++)
            {
                if(i == instance.instance_index)
                {
                    continue;
                }
                checkError(sem_wait(semaphore_ptrs[i]), 0, "sem_wait error");
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if(shared_memory_ptrs[i]->valid == true && shared_memory_ptrs[i]->mode == ModeType::FIT_TEST_MODE)
                {
                    if(shared_memory_ptrs[i]->fit_test_mode.ambient_y_axis_valid == true)
                    {
                        if(shared_memory_ptrs[i]->fit_test_mode.ambient_y_axis_min < ambient_y_axis_min)
                        {
                            ambient_y_axis_min = shared_memory_ptrs[i]->fit_test_mode.ambient_y_axis_min;
                        }
                        if(shared_memory_ptrs[i]->fit_test_mode.ambient_y_axis_max > ambient_y_axis_max)
                        {
                            ambient_y_axis_max = shared_memory_ptrs[i]->fit_test_mode.ambient_y_axis_max;
                        }
                    }
                    if(shared_memory_ptrs[i]->fit_test_mode.sample_y_axis_valid == true)
                    {
                        if(shared_memory_ptrs[i]->fit_test_mode.sample_y_axis_min < sample_y_axis_min)
                        {
                            sample_y_axis_min = shared_memory_ptrs[i]->fit_test_mode.sample_y_axis_min;
                        }
                        if(shared_memory_ptrs[i]->fit_test_mode.sample_y_axis_max > sample_y_axis_max)
                        {
                            sample_y_axis_max = shared_memory_ptrs[i]->fit_test_mode.sample_y_axis_max;
                        }
                    }
                    if(shared_memory_ptrs[i]->fit_test_mode.fit_factor_y_axis_valid == true)
                    {            
                        if(shared_memory_ptrs[i]->fit_test_mode.fit_factor_y_axis_min < fit_factor_y_axis_min)
                        {
                            fit_factor_y_axis_min = shared_memory_ptrs[i]->fit_test_mode.fit_factor_y_axis_min;
                        }
                        if(shared_memory_ptrs[i]->fit_test_mode.fit_factor_y_axis_max > fit_factor_y_axis_max)
                        {
                            fit_factor_y_axis_max = shared_memory_ptrs[i]->fit_test_mode.fit_factor_y_axis_max;
                        }
                    }            
                }
                std::atomic_thread_fence(std::memory_order_seq_cst);
                checkError(sem_post(semaphore_ptrs[i]), 0, "sem_post error");
            }

            const unsigned int ambient_y_axis_range = static_cast<unsigned int>(rint(ambient_y_axis_max - ambient_y_axis_min));
            const double ambient_y_axis_inc = 2.8 / static_cast<double>(ambient_y_axis_range);

            const unsigned int sample_y_axis_range = static_cast<unsigned int>(rint(sample_y_axis_max - sample_y_axis_min));
            const double sample_y_axis_inc = 2.8 / static_cast<double>(sample_y_axis_range);

            const unsigned int fit_factor_y_axis_range = static_cast<unsigned int>(rint(fit_factor_y_axis_max - fit_factor_y_axis_min));
            const double fit_factor_y_axis_inc = 2.8 / static_cast<double>(fit_factor_y_axis_range);

            draw_horizontal_log10_lines(axis_x_begin, axis_x_end, ambient_axis_y_begin, ambient_y_axis_inc, ambient_y_axis_range);
            draw_horizontal_log10_lines(axis_x_begin, axis_x_end, sample_axis_y_begin, sample_y_axis_inc, sample_y_axis_range);
            draw_horizontal_log10_lines(axis_x_begin, axis_x_end, fit_factor_axis_y_begin, fit_factor_y_axis_inc, fit_factor_y_axis_range);

            // draw x-axis label
            draw_horizontal_string("Time", 0.002, 4.5, 0.05);
            draw_horizontal_string("Time", 0.002, 4.5, 0.05 + axis_y_jump);
            draw_horizontal_string("Time", 0.002, 4.5, 0.05 + axis_y_jump * 2.0);

            // draw x-axis ticks
            char buf[32];
            for(unsigned int i = 0; i < x_axis_count; i+=2)
            {
                memset(buf, 0, sizeof(buf) );
                const double temp = rint(static_cast<double>(i) / x_axis_count_divisor * fit_test_mode_data.fit_test_mode_x_axis_max);
                static_assert(static_cast<int>(sizeof(buf) - 1) == sizeof(buf) - 1, "Size overflow");
                checkError3(snprintf(buf, sizeof(buf) - 1, "%u", static_cast<unsigned int>(temp ) ), static_cast<int>(sizeof(buf) - 1), "snprintf error");;
                draw_horizontal_string(buf, 0.001, axis_x_begin - 0.05 + x_axis_inc * static_cast<double>(i), 0.31);
                draw_horizontal_string(buf, 0.001, axis_x_begin - 0.05 + x_axis_inc * static_cast<double>(i), 0.31 + axis_y_jump);
                draw_horizontal_string(buf, 0.001, axis_x_begin - 0.05 + x_axis_inc * static_cast<double>(i), 0.31 + axis_y_jump * 2.0);
            }

            // draw y-axis label
            draw_vertical_string("Ambient", 0.002, 0.25, 1.5 - 0.2);
            draw_vertical_string("Mask", 0.002, 0.25, 1.5 + axis_y_jump);
            draw_vertical_string("Fit factor", 0.002, 0.25, 1.5 + axis_y_jump * 2.0 - 0.5);

            // draw y-axis ticks
            for(unsigned int i = 0; i <= ambient_y_axis_range; i++)
            {
                memset(buf, 0, sizeof(buf) );
                static_assert(static_cast<int>(sizeof(buf) - 1) == sizeof(buf) - 1, "Size overflow");
                checkError3(snprintf(buf, sizeof(buf) - 1, "1e%+d", static_cast<int>(ambient_y_axis_min) + static_cast<int>(i) ), static_cast<int>(sizeof(buf) - 1), "snprintf error");;
                draw_horizontal_string(buf, 0.001, 0.3, ambient_axis_y_begin + ambient_y_axis_inc * static_cast<double>(i) );
            }
            for(unsigned int i = 0; i <= sample_y_axis_range; i++)
            {
                memset(buf, 0, sizeof(buf) );
                static_assert(static_cast<int>(sizeof(buf) - 1) == sizeof(buf) - 1, "Size overflow");
                checkError3(snprintf(buf, sizeof(buf) - 1, "1e%+d", static_cast<int>(sample_y_axis_min) + static_cast<int>(i) ), static_cast<int>(sizeof(buf) - 1), "snprintf error");
                draw_horizontal_string(buf, 0.001, 0.3, sample_axis_y_begin + sample_y_axis_inc * static_cast<double>(i) );
            }       
            for(unsigned int i = 0; i <= fit_factor_y_axis_range; i++)
            {
                memset(buf, 0, sizeof(buf) );
                static_assert(static_cast<int>(sizeof(buf) - 1) == sizeof(buf) - 1, "Size overflow");
                checkError3(snprintf(buf, sizeof(buf) - 1, "1e%+d", static_cast<int>(fit_factor_y_axis_min) + static_cast<int>(i) ), static_cast<int>(sizeof(buf) - 1), "snprintf error");
                draw_horizontal_string(buf, 0.001, 0.3, fit_factor_axis_y_begin + fit_factor_y_axis_inc * static_cast<double>(i) );
            }       

            // draw data points
            glColor3d(color.R_value, color.G_value, color.B_value);
            glPointSize(8.0);
            glBegin(GL_POINTS);
            const double multiplier = 1.0 / fit_test_mode_data.fit_test_mode_x_axis_max;
            for(size_t i = 0, size = fit_test_mode_data.ambient_array.size(); i < size; i++)
            {
                const double x_coord = static_cast<double>(i) * multiplier * 9.0 + axis_x_begin;
                const double y_coord = (fit_test_mode_data.ambient_array[i] - ambient_y_axis_min) * ambient_y_axis_inc + ambient_axis_y_begin;
                glVertex3d(x_coord, y_coord, 0.1);
            }
            for(size_t i = 0, size = fit_test_mode_data.sample_array.size(); i < size; i++)
            {
                const double x_coord = static_cast<double>(i) * multiplier * 9.0 + axis_x_begin;
                const double y_coord = (fit_test_mode_data.sample_array[i] - sample_y_axis_min) * sample_y_axis_inc + sample_axis_y_begin;
                glVertex3d(x_coord, y_coord, 0.1);
            }
            for(size_t i = 0, size = fit_test_mode_data.fit_factor_array.size(); i < size; i++)
            {
                const double x_coord = static_cast<double>(i) * multiplier * 9.0 + axis_x_begin;
                const double y_coord = (fit_test_mode_data.fit_factor_array[i] - fit_factor_y_axis_min) * fit_factor_y_axis_inc + fit_factor_axis_y_begin;
                glVertex3d(x_coord, y_coord, 0.1);
            }
            glEnd();
        }

        // swap buffers
        glutSwapBuffers(); 
    }

    static void timer_func(const int value)
    {
        (void)value;

        fd_set selector;

        FD_ZERO(&selector);
        FD_SET(fds.serial_fd, &selector);

        timeval timeout = {0, 0};
        checkError2(select(fds.serial_fd + 1, &selector, NULL, NULL, &timeout), -1, "select error");

        char buf[300] = {0};
        ssize_t ret;
        if(FD_ISSET(fds.serial_fd, &selector) )
        {
            ret = read(fds.serial_fd, buf, sizeof(buf) - 1);
            checkError2(ret, -1L, "read error");
            if(ret > 0)
            {
                double val;
                printf("%s", buf);
                writeFully(fds.outfile_fd, buf, static_cast<size_t>(ret) );
                if(mode == ModeType::COUNT_MODE)
                {
                    if(sscanf(buf, "Conc. %lf #/cc", &val) == 1)
                    {
                        if(val == 0.0)
                        {
                            // change 0.0 to 0.001 to avoid log(0)
                            val = 0.001;
                        }
                        val = log10(val);
                        count_mode_data.count_array.push_back(val);
                        if(static_cast<double>(count_mode_data.count_array.size() ) > count_mode_data.count_mode_x_axis_max)
                        {
                            count_mode_data.count_mode_x_axis_max *= 2.0;
                        }
                        if(val < count_mode_data.count_array_min)
                        {
                            count_mode_data.count_array_min = val;
                        }
                        if(val > count_mode_data.count_array_max)
                        {
                            count_mode_data.count_array_max = val;
                        }
                    }
                }
                else if(mode == ModeType::FIT_TEST_MODE)
                {
                    if(sscanf(buf, "Mask %lf #/cc", &val) == 1)
                    {
                        val = log10(val);
                        fit_test_mode_data.sample_array.push_back(val);
                        if(static_cast<double>(fit_test_mode_data.sample_array.size() ) > fit_test_mode_data.fit_test_mode_x_axis_max)
                        {
                            fit_test_mode_data.fit_test_mode_x_axis_max *= 2.0;
                        }
                        if(val < fit_test_mode_data.sample_array_min)
                        {
                            fit_test_mode_data.sample_array_min = val;
                        }
                        if(val > fit_test_mode_data.sample_array_max)
                        {
                            fit_test_mode_data.sample_array_max = val;
                        }
                    }
                    else if(sscanf(buf, "Ambient %lf #/cc", &val) == 1)
                    {
                        val = log10(val);
                        fit_test_mode_data.ambient_array.push_back(val);
                        if(static_cast<double>(fit_test_mode_data.ambient_array.size() ) > fit_test_mode_data.fit_test_mode_x_axis_max)
                        {
                            fit_test_mode_data.fit_test_mode_x_axis_max *= 2.0;
                        }
                        if(val < fit_test_mode_data.ambient_array_min)
                        {
                            fit_test_mode_data.ambient_array_min = val;
                        }
                        if(val > fit_test_mode_data.ambient_array_max)
                        {
                            fit_test_mode_data.ambient_array_max = val;
                        }
                    }
                    else if(sscanf(buf, "FF %*u %lf PASS", &val) == 1 || sscanf(buf, "FF %*u %lf FAIL", &val) == 1)
                    {
                        val = log10(val);
                        fit_test_mode_data.fit_factor_array.push_back(val);
                        if(static_cast<double>(fit_test_mode_data.fit_factor_array.size() ) > fit_test_mode_data.fit_test_mode_x_axis_max)
                        {
                            fit_test_mode_data.fit_test_mode_x_axis_max *= 2.0;
                        }
                        if(val < fit_test_mode_data.fit_factor_array_min)
                        {
                            fit_test_mode_data.fit_factor_array_min = val;
                        }
                        if(val > fit_test_mode_data.fit_factor_array_max)
                        {
                            fit_test_mode_data.fit_factor_array_max = val;
                        }
                    }
                }
                // signal redraw
                glutPostRedisplay();
            }
        }

        glutTimerFunc(200, timer_func, 0);
    }

    static void init_graphics(void)
    {
        // clear to black
        glClearColor(1.0f, 1.0f, 1.0f, 0.0f);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_POINT_SMOOTH);
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_MULTISAMPLE);
        glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

        GLint actual_sample_count = 0;
        glGetIntegerv(GL_SAMPLES, &actual_sample_count);
        if(actual_sample_count != SAMPLE_COUNT)
        {
            printf("actual sample count = %d, requested sample count = %d\n", actual_sample_count, SAMPLE_COUNT);
        }

        // callbacks
        glutDisplayFunc(display);
        glutReshapeFunc(reshape);
        glutKeyboardFunc(keyboard_func);
        glutTimerFunc(200, timer_func, 0);
    }

    static inline int open_shared_memory_object(const char *const name, const off_t length, const int oflag)
    {
        int fd;
        struct stat statbuf;

        assertWithMsg(oflag == O_RDONLY || oflag == O_RDWR, "Unexpected file mode");
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0);
        if(fd == -1 && errno != EEXIST)
        {
            checkError2(fd, -1, "shm_open error");
        }
        else if(fd != -1)
        {
            checkError(flock(fd, LOCK_EX), 0, "flock error");
            checkError(ftruncate(fd, length), 0, "ftruncate error");
            checkError(fchmod(fd, S_IRUSR | S_IWUSR), 0, "fchmod error");
            checkError(flock(fd, LOCK_UN), 0, "flock error");
            checkError(close(fd), 0, "close error");
        }
        for(;;)
        {
            fd = shm_open(name, oflag, 0);
            if(fd != -1)
            {
                break;
            }
            else if(fd == -1 && errno != EACCES)
            {
                break;
            }
            checkError(usleep(100000), 0, "usleep error");
        }
        checkError2(fd, -1, "shm_open error");
        const uid_t current_euid = geteuid();
        const gid_t current_egid = getegid();
        checkError(fchmod(fd, S_IRUSR | S_IWUSR), 0, "fchmod error");
        checkError(fchown(fd, current_euid, current_egid), 0, "fchown error");
        checkError(flock(fd, LOCK_SH), 0, "flock error");
        checkError(fstat(fd, &statbuf), 0, "fstat error");
        assertWithMsg(S_ISREG(statbuf.st_mode) && (statbuf.st_mode & 07777) == (S_IRUSR | S_IWUSR) && statbuf.st_size == length && statbuf.st_uid == current_euid && statbuf.st_gid == current_egid, "Unexpected file stat");
        return fd;
    }

    static inline void atomic_test_and_set(std::atomic<bool> &val, const bool old_val, const bool new_val)
    {
        assertWithMsg(val.exchange(new_val, std::memory_order_seq_cst) == old_val, "Unexpected value");      
    }

    static void init_shared_memory(void)
    {
        char name_buf_sem[250];
        char name_buf_data[250];
        int sem_fd, data_fd;
        void *reserve_ptr, *sem_ptr, *data_ptr;
        const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));

        assertWithMsg(sizeof(sem_t) <= page_size && sizeof(SharedMemoryBuffer) <= page_size, "Unexpected page size");
        semaphore_ptrs = new sem_t*[instance.total_instances];
        memset(semaphore_ptrs, 0, sizeof(sem_t *) * instance.total_instances);

        shared_memory_ptrs = new SharedMemoryBuffer*[instance.total_instances];
        memset(shared_memory_ptrs, 0, sizeof(SharedMemoryBuffer *) * instance.total_instances);

        for(unsigned int i = 0; i < instance.total_instances; i++)
        {
            memset(name_buf_sem, 0, sizeof(name_buf_sem));
            memset(name_buf_data, 0, sizeof(name_buf_data));
            static_assert(static_cast<int>(sizeof(name_buf_sem) - 1) == sizeof(name_buf_sem) - 1, "Size overflow");
            static_assert(static_cast<int>(sizeof(name_buf_data) - 1) == sizeof(name_buf_data) - 1, "Size overflow");
            checkError3(snprintf(name_buf_sem, sizeof(name_buf_sem) - 1, "%s_semaphore_%u", shared_memory_prefix, i), static_cast<int>(sizeof(name_buf_sem) - 1), "snprintf error");
            checkError3(snprintf(name_buf_data, sizeof(name_buf_data) - 1, "%s_data_%u", shared_memory_prefix, i), static_cast<int>(sizeof(name_buf_data) - 1), "snprintf error");
            sem_fd = open_shared_memory_object(name_buf_sem, sizeof(sem_t), O_RDWR);
            data_fd = open_shared_memory_object(name_buf_data, sizeof(SharedMemoryBuffer), ((instance.instance_index == i) ? (O_RDWR) : (O_RDONLY) ) );
            reserve_ptr = mmap(NULL, page_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            checkError2(reserve_ptr, MAP_FAILED, "mmap error");
            sem_ptr = mmap(reserve_ptr, sizeof(sem_t), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_FIXED, sem_fd, 0);
            checkError2(sem_ptr, MAP_FAILED, "mmap error");
            semaphore_ptrs[i] = static_cast<sem_t *>(sem_ptr);
            reserve_ptr = mmap(NULL, page_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            checkError2(reserve_ptr, MAP_FAILED, "mmap error");
            data_ptr = mmap(reserve_ptr, sizeof(SharedMemoryBuffer), ((instance.instance_index == i) ? (PROT_WRITE | PROT_READ) : (PROT_READ) ), MAP_SHARED | MAP_FIXED, data_fd, 0);
            checkError2(data_ptr, MAP_FAILED, "mmap error");
            shared_memory_ptrs[i] = static_cast<SharedMemoryBuffer *>(data_ptr);
            checkError(flock(sem_fd, LOCK_UN), 0, "flock error");
            checkError(flock(data_fd, LOCK_UN), 0, "flock error");
            checkError(close(sem_fd), 0, "close error");
            checkError(close(data_fd), 0, "close error");
        }

        checkError(sem_init(semaphore_ptrs[instance.instance_index], 1, 1), 0, "sem_init error");
        memset(shared_memory_ptrs[instance.instance_index], 0, sizeof(SharedMemoryBuffer) );
        atomic_test_and_set(shared_memory_ptrs[instance.instance_index]->initialized, false, true);
        std::atomic_thread_fence(std::memory_order_seq_cst);

        for(unsigned int i = 0; i < instance.total_instances; i++)
        {
            if(instance.instance_index == i)
            {
                continue;
            }
            for(;;)
            {
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if(shared_memory_ptrs[i]->initialized.load(std::memory_order_seq_cst) == true)
                {
                    break;
                }
                checkError(usleep(100000), 0, "usleep error");
            }
        }
    }

    static void remove_shared_memory(void)
    {
        char name_buf_sem[250];
        char name_buf_data[250];
        const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));

        shared_memory_ptrs[instance.instance_index]->valid = false;
        shared_memory_ptrs[instance.instance_index]->count_mode.y_axis_valid = false;
        shared_memory_ptrs[instance.instance_index]->fit_test_mode.ambient_y_axis_valid = false;
        shared_memory_ptrs[instance.instance_index]->fit_test_mode.sample_y_axis_valid = false;
        shared_memory_ptrs[instance.instance_index]->fit_test_mode.fit_factor_y_axis_valid = false;
        atomic_test_and_set(shared_memory_ptrs[instance.instance_index]->quit, false, true);
        std::atomic_thread_fence(std::memory_order_seq_cst);

        for(unsigned int i = 0; i < instance.total_instances; i++)
        {
            if(instance.instance_index == i)
            {
                continue;
            }
            for(;;)
            {
                std::atomic_thread_fence(std::memory_order_seq_cst);
                if(shared_memory_ptrs[i]->quit.load(std::memory_order_seq_cst) == true)
                {
                    break;
                }
                checkError(usleep(100000), 0, "usleep error");
            }
        }

        checkError(sem_destroy(semaphore_ptrs[instance.instance_index]), 0, "sem_destroy error");

        for(unsigned int i = 0; i < instance.total_instances; i++)
        {
            checkError(munmap(semaphore_ptrs[i],  page_size * 2), 0, "munmap error");
            checkError(munmap(shared_memory_ptrs[i],  page_size * 2), 0, "munmap error");

            int ret;
            memset(name_buf_sem, 0, sizeof(name_buf_sem));
            memset(name_buf_data, 0, sizeof(name_buf_data));
            static_assert(static_cast<int>(sizeof(name_buf_sem) - 1) == sizeof(name_buf_sem) - 1, "Size overflow");
            static_assert(static_cast<int>(sizeof(name_buf_data) - 1) == sizeof(name_buf_data) - 1, "Size overflow");
            checkError3(snprintf(name_buf_sem, sizeof(name_buf_sem) - 1, "%s_semaphore_%u", shared_memory_prefix, i), static_cast<int>(sizeof(name_buf_sem) - 1), "snprintf error");
            checkError3(snprintf(name_buf_data, sizeof(name_buf_data) - 1, "%s_data_%u", shared_memory_prefix, i), static_cast<int>(sizeof(name_buf_data) - 1), "snprintf error");
            ret = shm_unlink(name_buf_sem);
            if(ret != 0 && errno != ENOENT)
            {
                checkError(ret, 0, "shm_unlink error");
            }
            ret = shm_unlink(name_buf_data);
            if(ret != 0 && errno != ENOENT)
            {
                checkError(ret, 0, "shm_unlink error");
            }
        }

        delete [] shared_memory_ptrs;
        shared_memory_ptrs = NULL;
        delete [] semaphore_ptrs;
        semaphore_ptrs = NULL;
    }
}

int main(int argc, char *argv[])
{
    termios config, config2;
    speed_t baud_rate;
    double temp_dbl;
    long int temp_long;

    assertWithMsg(argc >= 11, "Need more arguments: <device> <baud rate> <output_file> <window_x> <window_y> <R_value> <G_value> <B_value> <total_instances> <instance_index>");

    count_mode_data.count_array.reserve(20);
    fit_test_mode_data.ambient_array.reserve(20);
    fit_test_mode_data.sample_array.reserve(20);
    fit_test_mode_data.fit_factor_array.reserve(20);

    temp_long = strtol(argv[4], NULL, 10);
    assertWithMsg(temp_long >= 0 && temp_long <= 5000, "window_x out of range");
    const int window_x = static_cast<int>(temp_long);

    temp_long = strtol(argv[5], NULL, 10);
    assertWithMsg(temp_long >= 0 && temp_long <= 3000, "window_y out of range");
    const int window_y = static_cast<int>(temp_long);

    temp_dbl = strtod(argv[6], NULL);
    assertWithMsg(temp_dbl >= 0.0 && temp_dbl <= 1.0, "R_value out of range");
    color.R_value = temp_dbl;

    temp_dbl = strtod(argv[7], NULL);
    assertWithMsg(temp_dbl >= 0.0 && temp_dbl <= 1.0, "G_value out of range");
    color.G_value = temp_dbl;

    temp_dbl = strtod(argv[8], NULL);
    assertWithMsg(temp_dbl >= 0.0 && temp_dbl <= 1.0, "B_value out of range");
    color.B_value = temp_dbl;

    temp_long = strtol(argv[9], NULL, 10);
    assertWithMsg(temp_long > 0 && temp_long <= 10000, "total_instances out of range");
    instance.total_instances = static_cast<unsigned int>(temp_long);

    temp_long = strtol(argv[10], NULL, 10);
    assertWithMsg(temp_long >= 0 && temp_long <= 10000, "instance_index out of range");
    instance.instance_index = static_cast<unsigned int>(temp_long);
    assertWithMsg(instance.instance_index < instance.total_instances, "instance_index must be less than total_instances");

    if(strcmp(argv[2], "300") == 0)
    {
        baud_rate = B300;
    }
    else if(strcmp(argv[2], "600") == 0)
    {
        baud_rate = B600;
    }
    else if(strcmp(argv[2], "1200") == 0)
    {
        baud_rate = B1200;
    }
    else if(strcmp(argv[2], "2400") == 0)
    {
        baud_rate = B2400;
    }
    else if(strcmp(argv[2], "9600") == 0)
    {
        baud_rate = B9600;
    }
    else
    {
        fprintf(stderr, "Invalid baud rate\n");
        return 1;
    }

    fds.serial_fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOCTTY);
    checkError2(fds.serial_fd, -1, "open error");

    checkError(isatty(fds.serial_fd), 1, "isatty error");

    fds.outfile_fd = open(argv[3], O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    checkError2(fds.outfile_fd, -1, "open error");

    memset(&config, 0, sizeof(config));
    checkError(tcgetattr(fds.serial_fd, &config), 0, "tcgetattr error");

    // 8 data bits, 1 stop bit, no parity
    config.c_cflag &= static_cast<tcflag_t>(~(CSIZE | CSTOPB | PARENB));
    config.c_cflag |= CS8;

    // don't map CR to NL or vice versa
    config.c_iflag &= static_cast<tcflag_t>(~(ICRNL | INLCR));

    // set baud rate
    checkError(cfsetispeed(&config, baud_rate), 0, "cfsetispeed error");
    checkError(cfsetospeed(&config, baud_rate), 0, "cfsetospeed error");

    checkError(tcsetattr(fds.serial_fd, TCSANOW, &config), 0, "tcsetattr error");

    memset(&config2, 0, sizeof(config2));
    checkError(tcgetattr(fds.serial_fd, &config2), 0, "tcgetattr error");

    checkError(memcmp(&config, &config2, sizeof(config)), 0, "memcmp error"); 

    checkError(ioctl(fds.serial_fd, TIOCEXCL, NULL), 0, "ioctl error");

    // set up shared memory regions
    init_shared_memory();

    // set up graphical window
    glutInit(&argc, argv);
    glutSetOption(GLUT_MULTISAMPLE, SAMPLE_COUNT);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH | GLUT_MULTISAMPLE);
    glutInitWindowPosition(window_x, window_y);
    glutInitWindowSize(window.window_width, window.window_height);
    glutCreateWindow("Portacount window");

    init_graphics();

    glutMainLoop();

    checkError(close(fds.serial_fd), 0, "close error");
    checkError(close(fds.outfile_fd), 0, "close error");
    remove_shared_memory();
    return 0;
}
