// ======================================================================== //
// Copyright 2018-2019 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include <iterator>
#include <memory>
#include <random>
#include <mpi.h>
#include "GLFWDistribOSPRayWindow.h"

#include "ospcommon/library.h"
#include "ospray_testing.h"

#include <imgui.h>

using namespace ospcommon;

// Generate the rank's local spheres within its assigned grid cell
OSPGeometry makeLocalSpheres(const int mpiRank, const int mpiWorldSize);

int main(int argc, char **argv)
{
  int mpiThreadCapability = 0;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mpiThreadCapability);
  if (mpiThreadCapability != MPI_THREAD_MULTIPLE
      && mpiThreadCapability != MPI_THREAD_SERIALIZED)
  {
    fprintf(stderr, "OSPRay requires the MPI runtime to support thread "
            "multiple or thread serialized.\n");
    return 1;
  }

  int mpiRank = 0;
  int mpiWorldSize = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpiRank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpiWorldSize);

  std::cout << "OSRay rank " << mpiRank << "/" << mpiWorldSize << "\n";

  // load the MPI module, and select the MPI distributed device. Here we
  // do not call ospInit, as we want to explicitly pick the distributed
  // device. This can also be done by passing --osp:mpi-distributed when
  // using ospInit, however if the user doesn't pass this argument your
  // application will likely not behave as expected
  ospLoadModule("mpi");

  OSPDevice mpiDevice = ospNewDevice("mpi_distributed");
  ospDeviceCommit(mpiDevice);
  ospSetCurrentDevice(mpiDevice);

  // set an error callback to catch any OSPRay errors and exit the application
  ospDeviceSetErrorFunc(
      ospGetCurrentDevice(), [](OSPError error, const char *errorDetails) {
        std::cerr << "OSPRay error: " << errorDetails << std::endl;
        exit(error);
      });

  // create the "world" model which will contain all of our geometries
  OSPModel world = ospNewModel();

  // all ranks specify the same rendering parameters, with the exception of
  // the data to be rendered, which is distributed among the ranks
  OSPGeometry spheres = makeLocalSpheres(mpiRank, mpiWorldSize);
  ospAddGeometry(world, spheres);
  ospRelease(spheres);

  ospSet1i(world, "id", mpiRank);
  // commit the world model
  ospCommit(world);

  // create OSPRay renderer
  OSPRenderer renderer = ospNewRenderer("mpi_raycast");

  // create and setup an ambient light
  std::array<OSPLight, 2> lights = {
    ospNewLight3("ambient"),
    ospNewLight3("distant")
  };
  ospCommit(lights[0]);

  ospSet3f(lights[1], "direction", -1.f, -1.f, 0.5f);
  ospCommit(lights[1]);

  OSPData lightData = ospNewData(lights.size(), OSP_LIGHT, lights.data(), 0);
  ospCommit(lightData);
  ospSetObject(renderer, "lights", lightData);
  ospRelease(lightData);

  // create a GLFW OSPRay window: this object will create and manage the OSPRay
  // frame buffer and camera directly
  auto glfwOSPRayWindow =
      std::unique_ptr<GLFWDistribOSPRayWindow>(new GLFWDistribOSPRayWindow(
          vec2i{1024, 768}, box3f(vec3f(-1.f), vec3f(1.f)), world, renderer));

  int spp = 1;
  int currentSpp = 1;
  if (mpiRank == 0) {
    glfwOSPRayWindow->registerImGuiCallback([&]() {
      ImGui::SliderInt("spp", &spp, 1, 64);
    });
  }

  glfwOSPRayWindow->registerDisplayCallback([&](GLFWDistribOSPRayWindow *win) {
    // Send the UI changes out to the other ranks so we can synchronize
    // how many samples per-pixel we're taking
    MPI_Bcast(&spp, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (spp != currentSpp) {
      currentSpp = spp;
      ospSet1i(renderer, "spp", spp);
      win->addObjectToCommit(renderer);
    }
  });

  // start the GLFW main loop, which will continuously render
  glfwOSPRayWindow->mainLoop();

  // cleanup remaining objects
  ospRelease(world);
  ospRelease(renderer);

  // cleanly shut OSPRay down
  ospShutdown();

  MPI_Finalize();

  return 0;
}

bool computeDivisor(int x, int &divisor)
{
  int upperBound = std::sqrt(x);
  for (int i = 2; i <= upperBound; ++i) {
    if (x % i == 0) {
      divisor = i;
      return true;
    }
  }
  return false;
}

// Compute an X x Y x Z grid to have 'num' grid cells,
// only gives a nice grid for numbers with even factors since
// we don't search for factors of the number, we just try dividing by two
vec3i computeGrid(int num)
{
  vec3i grid(1);
  int axis = 0;
  int divisor = 0;
  while (computeDivisor(num, divisor)) {
    grid[axis] *= divisor;
    num /= divisor;
    axis = (axis + 1) % 3;
  }
  if (num != 1) {
    grid[axis] *= num;
  }
  return grid;
}

OSPGeometry makeLocalSpheres(const int mpiRank, const int mpiWorldSize) {
  struct Sphere
  {
    vec3f org;
  };

  const float sphereRadius = 0.1;
  std::vector<Sphere> spheres(10);

  // To simulate loading a shared dataset all ranks generate the same
  // sphere data.
  std::random_device rd;
  std::mt19937 rng(rd());

  const vec3i grid = computeGrid(mpiWorldSize);
  const vec3i brickId(mpiRank % grid.x,
                      (mpiRank / grid.x) % grid.y,
                      mpiRank / (grid.x * grid.y));

  // The grid is over the [-1, 1] box
  const vec3f brickSize = vec3f(2.0) / vec3f(grid);
  const vec3f brickLower = brickSize * brickId - vec3f(1.f);
  const vec3f brickUpper = brickSize * brickId - vec3f(1.f) + brickSize;

  // Generate spheres within the box padded by the radius, so we don't need
  // to worry about ghost bounds
  std::uniform_real_distribution<float> distX(brickLower.x + sphereRadius,
      brickUpper.x - sphereRadius);
  std::uniform_real_distribution<float> distY(brickLower.y + sphereRadius,
      brickUpper.y - sphereRadius);
  std::uniform_real_distribution<float> distZ(brickLower.z + sphereRadius,
      brickUpper.z - sphereRadius);

  for (auto &s : spheres) {
    s.org.x = distX(rng);
    s.org.y = distY(rng);
    s.org.z = distZ(rng);
  }

  OSPData sphereData = ospNewData(spheres.size() * sizeof(Sphere), OSP_UCHAR,
                                  spheres.data());


  vec3f color(0.f, 0.f, (mpiRank + 1.f) / mpiWorldSize);
  OSPMaterial material = ospNewMaterial2("scivis", "OBJMaterial");
  ospSet3fv(material, "Kd", &color.x);
  ospSet3f(material, "Ks", 1.f, 1.f, 1.f);
  ospCommit(material);

  OSPGeometry sphereGeom = ospNewGeometry("spheres");
  ospSet1i(sphereGeom, "bytes_per_sphere", int(sizeof(Sphere)));
  ospSet1f(sphereGeom, "radius", sphereRadius);
  ospSetData(sphereGeom, "spheres", sphereData);
  ospSetMaterial(sphereGeom, material);
  ospRelease(material);
  ospRelease(sphereData);
  ospCommit(sphereGeom);

  return sphereGeom;
}
