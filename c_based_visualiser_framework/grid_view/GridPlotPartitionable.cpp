#include <GL/glut.h>
#include <GL/freeglut.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <deque>
#include "GridPlotPartitionable.h"
#include "../utilities/colour.h"
#include "../glut_framework/GlutFramework.h"


GridPlotPartitionable::GridPlotPartitionable(
        int argc, char **argv, int grid_size_x, int grid_size_y,
        GridColourReader *colour_reader, bool wait_for_start) {

    this->window_width = INIT_WINDOW_WIDTH;
    this->window_height = INIT_WINDOW_HEIGHT;
    this->user_pressed_start = !wait_for_start;
    this->simulation_started = false;
    this->database_read = false;

    this->timestep_ms = 0;
    this->points_recieved = 0;
    this->plot_time_ms = 0;
    this->grid_size_x = grid_size_x;
    this->grid_size_y = grid_size_y;
    this->n_cells = grid_size_x * grid_size_y;
    this->colour_reader = colour_reader;

    this->argc = argc;
    this->argv = argv;

    // define queue for the incoming state changes
    this->states_to_draw = new std::deque<state_to_draw_struct>(
        this->grid_size_x * this->grid_size_y);

    // create mutexs
    if (pthread_mutex_init(&(this->start_mutex), NULL) == -1) {
        fprintf(stderr, "Error initializing start mutex!\n");
        exit(-1);
    }
    if (pthread_cond_init(&(this->start_condition), NULL) == -1) {
        fprintf(stderr, "Error initializing start condition!\n");
        exit(-1);
    }

    if (pthread_mutex_init(&(this->point_mutex), NULL) == -1) {
        fprintf(stderr, "Error initializing point mutex!\n");
        exit(-1);
    }
}

void GridPlotPartitionable::main_loop() {
    startFramework(argc, argv, "Grid", window_width, window_height,
                   INIT_WINDOW_X, INIT_WINDOW_Y, FRAMES_PER_SECOND);
}

void GridPlotPartitionable::init() {
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glColor3f(1.0, 1.0, 1.0);
    glShadeModel(GL_SMOOTH);
}

void GridPlotPartitionable::init_population(
        char *label, int n_cells, float run_time_ms,
        float machine_time_step_ms) {
    std::string label_str = std::string(label);
    this->plot_time_ms = run_time_ms;
    this->timestep_ms = machine_time_step_ms;

    char *cell_label = (char *) malloc(sizeof(char) * strlen(label));
    strcpy(cell_label, label);
    this->cell_label = cell_label;

    pthread_mutex_lock(&(this->start_mutex));
    if (this->n_cells == n_cells) {
        this->database_read = true;
        while (!this->user_pressed_start) {
            pthread_cond_wait(&(this->start_condition), &(this->start_mutex));
        }
    }
    else{
       fprintf(stderr,
              "Error vertex to listen to does not have enough elements!\n");
        exit(-1);
    }
    pthread_mutex_unlock(&(this->start_mutex));
}

void GridPlotPartitionable::events_start(
        char *label, SpiNNakerFrontEndCommonLiveEventsConnection *connection) {
    pthread_mutex_lock(&(this->start_mutex));
    this->simulation_started = true;
    pthread_mutex_unlock(&(this->start_mutex));
}

void GridPlotPartitionable::receive_events(
        char *label, int cell_id, int payload) {

    pthread_mutex_lock(&(this->point_mutex));
    std::string label_str = std::string(label);

    // find coords
    int x_coord = cell_id % this->grid_size_y;
    int y_coord = cell_id / this->grid_size_y;

    fprintf(stderr, "cell %i, [%i,%i]\n", cell_id, x_coord, y_coord);

    state_to_draw_struct *data_item;
    data_item->cell_id = cell_id;
    data_item->state = payload;

    this->states_to_draw->push_back(*data_item);
    this->points_recieved ++;

    if( this->points_recieved >= this->grid_size_y * this->grid_size_x){
        this->timestep_ms ++;
        this->points_recieved = 0;
    }
    pthread_mutex_unlock(&(this->point_mutex));
}

//-------------------------------------------------------------------------
//  Draws a string at the specified coordinates.
//-------------------------------------------------------------------------
void GridPlotPartitionable::printgl(float x, float y, void *font_style,
        char* format, ...) {
    va_list arg_list;
    char str[256];
    int i;

    va_start(arg_list, format);
    vsprintf(str, format, arg_list);
    va_end(arg_list);

    glRasterPos2f(x, y);

    for (i = 0; str[i] != '\0'; i++) {
        glutBitmapCharacter(font_style, str[i]);
    }
}

void GridPlotPartitionable::printglstroke(
        float x, float y, float size, float rotate, char* format, ...) {
    va_list arg_list;
    char str[256];
    int i;
    GLvoid *font_style = GLUT_STROKE_ROMAN;

    va_start(arg_list, format);
    vsprintf(str, format, arg_list);
    va_end(arg_list);

    glPushMatrix();
    glEnable (GL_BLEND);   // antialias the font
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable (GL_LINE_SMOOTH);
    glLineWidth(1.5);   // end setup for antialiasing
    glTranslatef(x, y, 0);
    glScalef(size, size, size);
    glRotatef(rotate, 0.0, 0.0, 1.0);
    for (i = 0; str[i] != '\0'; i++) {
        glutStrokeCharacter(GLUT_STROKE_ROMAN, str[i]);
    }
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void GridPlotPartitionable::display(float time) {
    if (glutGetWindow() == this->window) {

        glPointSize(1.0);

        glClearColor(1.0, 1.0, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor4f(0.0, 0.0, 0.0, 1.0);

        // figure out the cell sizes based off the window size
        float cell_width = (window_width -
                            (2 * WINDOW_BORDER)) / this->grid_size_x;
        float cell_height = (window_height -
                             (2 * WINDOW_BORDER)) / this->grid_size_y;

        // handle the database connection stuff
        pthread_mutex_lock(&(this->start_mutex));
        if (!this->database_read) {
            char prompt[] = "Waiting for database to be ready...";
            printgl((window_width / 2) - 120, window_height - 50,
                    GLUT_BITMAP_TIMES_ROMAN_24, prompt);
        } else if (!this->user_pressed_start) {
            char prompt[] = "Press space bar to start...";
            printgl((window_width / 2) - 100, window_height - 50,
                    GLUT_BITMAP_TIMES_ROMAN_24, prompt);
        } else if (!this->simulation_started) {
            char prompt[] = "Waiting for simulation to start...";
            printgl((window_width / 2) - 120, window_height - 50,
                    GLUT_BITMAP_TIMES_ROMAN_24, prompt);
        } else {
            char title[] = "Grid";
            printgl((window_width / 2) - 75, window_height - 50,
                    GLUT_BITMAP_TIMES_ROMAN_24, title);
        }
        pthread_mutex_unlock(&(this->start_mutex));

        // Draw the grid lines
        glColor4f(0.0, 0.0, 0.0, 1.0);
        for (uint32_t i = 0; i <= this->grid_size_x; i++) {
            if (i % this->grid_size_x == 0) {
                glLineWidth(3.0);
            } else {
                glLineWidth(1.0);
            }
            glBegin (GL_LINES);
            uint32_t y_pos = WINDOW_BORDER + (i * cell_height);
            glVertex2f(window_width - WINDOW_BORDER, y_pos);
            glVertex2f(WINDOW_BORDER, y_pos);
            glEnd();
        }

        for (uint32_t i = 0; i <= this->grid_size_y; i++) {
            if (i % this->grid_size_y == 0) {
                glLineWidth(3.0);
            } else {
                glLineWidth(1.0);
            }
            glBegin (GL_LINES);
            uint32_t x_pos = WINDOW_BORDER + (i * cell_width);
            glVertex2f(x_pos, window_height - WINDOW_BORDER);
            glVertex2f(x_pos, WINDOW_BORDER);
            glEnd();
        }

        pthread_mutex_lock(&(this->point_mutex));

        // Work out the cell values
        int cell_value[81];
        for (uint32_t cell = 0; cell < 81; cell++) {

            // Strip off items that are no longer needed
            while (!points_to_draw[cell].empty() &&
                    points_to_draw[cell].front().first < start_tick) {
                points_to_draw[cell].pop_front();
            }

            // Count the events per number
            int count[9];
            for (uint32_t i = 0; i < 9; i++) {
                count[i] = 0;
            }
            int total = 0;
            for (std::deque<std::pair<int, int> >::iterator iter =
                    points_to_draw[cell].begin();
                    iter != points_to_draw[cell].end(); ++iter) {
                int number = iter->second / this->neurons_per_number;
                if (number < 9) {
                    count[number] += 1;
                    total += 1;
                } else {
                    fprintf(
                        stderr, "Neuron id %d out of range\n", iter->second);
                }
            }
        }



        // Print the events
        for (uint32_t cell = 0; cell < 81; cell++) {
            uint32_t cell_x = cell % 9;
            uint32_t cell_y = cell / 9;

            uint32_t x_start = WINDOW_BORDER + (cell_x * cell_width) + 1;
            uint32_t y_start = WINDOW_BORDER + (cell_y * cell_height) + 1;
            float y_spacing = (float) cell_height
                    / (float) (this->neurons_per_number * 9);

            // Work out how probable the number is and use this for colouring
            float cell_sat = 1 - cell_prob[cell];

            glPointSize(2.0);
            glBegin(GL_POINTS);

            if (cell_valid[cell]) {
                glColor4f(cell_sat, 1.0, cell_sat, 1.0);
            } else {
                glColor4f(1.0, cell_sat, cell_sat, 1.0);
            }

            for (std::deque<std::pair<int, int> >::iterator iter =
                    points_to_draw[cell].begin();
                    iter != points_to_draw[cell].end(); ++iter) {
                float x_value = ((iter->first - start_tick) * x_spacing)
                                 + x_start;
                float y_value = (iter->second * y_spacing) + y_start;
                glVertex2f(x_value, y_value);
            }

            glEnd();

            // Print the number
            if (cell_value[cell] != 0) {
                glColor4f(0, 0, 0, 1 - cell_sat);
                char label[] = "%d";
                float size = 0.005 * cell_height;
                printglstroke(
                    x_start + (cell_width / 2.0) - (size * 50.0),
                    y_start + (cell_height / 2.0) - (size * 50.0),
                    size, 0, label, cell_value[cell]);
            }
        }

        pthread_mutex_unlock(&(this->point_mutex));

        glutSwapBuffers();
    }
}

void GridPlotPartitionable::reshape(int width, int height) {
    if (glutGetWindow() == this->window) {
        fprintf(stderr, "Reshape to %d, %d\n", width, height);
        this->window_width = width;
        this->window_height = height;

        // viewport dimensions
        glViewport(0, 0, (GLsizei) width, (GLsizei) height);
        glMatrixMode (GL_PROJECTION);
        glLoadIdentity();

        // an orthographic projection. Should probably look into OpenGL
        // perspective projections for 3D if that's your thing
        glOrtho(0.0, width, 0.0, height, -50.0, 50.0);
        glMatrixMode (GL_MODELVIEW);
        glLoadIdentity();
    }
}

void GridPlotPartitionable::keyboardUp(unsigned char key, int x, int y) {
    if ((int) key == 32) {

        // create and send the eieio command message confirming database
        // read
        pthread_mutex_lock(&(this->start_mutex));
        if (!this->user_pressed_start) {
            printf("Starting the simulation\n");
            this->user_pressed_start = true;
            pthread_cond_signal(&(this->start_condition));
        }
        pthread_mutex_unlock(&(this->start_mutex));
    }
}

void GridPlotPartitionable::safelyshut(void) {
    exit(0);                // kill program dead
}

GridPlotPartitionable::~GridPlotPartitionable() {
    // TODO Auto-generated destructor stub
}
