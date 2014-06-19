// obj
#include "objrenderer.h"
#include "objmaterial.h"
#include "objpointlight.h"
// ospray
#include "ospray/common/model.h"
#include "ospray/common/data.h"
#include "ospray/camera/camera.h"
//embree
#include "embree2/rtcore.h"

namespace ospray {
  namespace obj {

    extern "C" void ispc__OBJRenderer_renderTile(void *tile, void *camera, void *model);

    void OBJRenderer::RenderTask::renderTile(Tile &tile)
    {
      ispc__OBJRenderer_renderTile(&tile,
                                   camera->getIE(),
                                   world->getIE());
    }
    
    TileRenderer::RenderJob *OBJRenderer::createRenderJob(FrameBuffer *fb)
    {
      RenderTask *frame = new RenderTask;
      frame->world = (Model *)getParamObject("world",NULL);
      Assert2(frame->world,"null world handle (did you forget to assign a "
              "'world' parameter to the ray_cast renderer?)");

      frame->camera = (Camera *)getParamObject("camera",NULL);
      Assert2(frame->camera,"null camera handle (did you forget to assign a "
              "'camera' parameter to the ray_cast renderer?)");

      return frame;
    }
    
    /*! \brief create a material of given type */
    Material *OBJRenderer::createMaterial(const char *type)
    {
      Material *mat = new OBJMaterial;
      return mat;
    }

    /*! \brief create a light of given type */
    Light *OBJRenderer::createLight(const char *type)
    {
      if(!strcmp("PointLight", type)) {
        Light *light = new OBJPointLight;
        return light;
      }
      return NULL;
    }

    OSP_REGISTER_RENDERER(OBJRenderer,OBJ);
    OSP_REGISTER_RENDERER(OBJRenderer,obj);
  }
}
