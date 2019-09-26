// ======================================================================== //
// Copyright 2009-2019 Intel Corporation                                    //
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

#include "VKLSharedStructuredVolume.h"
#include "common/Data.h"

namespace ospray {

  std::string VKLSharedStructuredVolume::toString() const
  {
    return "ospray::volume::VKLSharedStructuredVolume";
  }

  void VKLSharedStructuredVolume::commit()
  {
    ispcEquivalent = ispc::Volume_createInstance_vklVolume(this);
    vklVolume      = vklNewVolume("structured_regular");

    handleParams();

    vklCommit(vklVolume);
    (vkl_box3f &)bounds = vklGetBoundingBox(vklVolume);
    Volume::commit();
    ispc::Volume_set_vklVolume(
        ispcEquivalent, vklVolume, (ispc::box3f *)&bounds);
  }

  OSP_REGISTER_VOLUME(VKLSharedStructuredVolume, vkl_structured_volume);
  OSP_REGISTER_VOLUME(VKLSharedStructuredVolume, shared_structured_volume);

}  // namespace ospray
