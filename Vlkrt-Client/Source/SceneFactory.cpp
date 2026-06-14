#include "SceneFactory.h"

#include <glm/gtc/matrix_transform.hpp>
#include <random>

namespace Vlkrt
{
    auto SceneFactory::CreateCornellBox(CameraHint* camHint) -> Scene
    {
        Scene scene;
        scene.SceneIndex              = SCENE_CORNELL_BOX;
        scene.RaytracingType          = RaytracingMode::PathTracing;
        scene.ImportanceSampling      = ImportanceSamplingMode::BSDF;
        scene.MaxRecursionDepth       = 12;
        scene.MaxShadowRecursionDepth = scene.MaxRecursionDepth;
        scene.PathSqrtSamplesPerPixel = 1;
        scene.ApplyJitter             = true;
        scene.OnlyOneLightSample      = false;
        scene.RussianRouletteDepth    = 3;
        scene.AnisotropicBSDF         = true;
        scene.BackgroundColor         = glm::vec3(0.0f);

        if (camHint) {
            camHint->Eye    = glm::vec3(0.0f, 3.5f, 8.0f);
            camHint->Target = glm::vec3(0.0f, 2.5f, 0.0f);
        }

        // Materials
        // Index 0: white floor/ceiling
        {
            Material m;
            m.Name          = "cb_floor";
            m.Albedo        = glm::vec3(0.9f);
            m.Roughness     = 0.4f;
            m.MaterialIndex = 0;
            scene.Materials.push_back(m);
        }
        // Index 1: white wall (back, top/bottom)
        {
            Material m;
            m.Name          = "cb_white_wall";
            m.Albedo        = glm::vec3(0.9f);
            m.Roughness     = 1.0f;
            m.MaterialIndex = 1;
            scene.Materials.push_back(m);
        }
        // Index 2: green wall (left)
        {
            Material m;
            m.Name          = "cb_green_wall";
            m.Albedo        = glm::vec3(0.2f, 0.8f, 0.2f);
            m.Roughness     = 1.0f;
            m.MaterialIndex = 2;
            scene.Materials.push_back(m);
        }
        // Index 3: red wall (right)
        {
            Material m;
            m.Name          = "cb_red_wall";
            m.Albedo        = glm::vec3(0.8f, 0.1f, 0.1f);
            m.Roughness     = 1.0f;
            m.MaterialIndex = 3;
            scene.Materials.push_back(m);
        }
        // Index 4: light emission material (area light mesh)
        {
            Material m;
            m.Name          = "cb_light";
            m.Albedo        = glm::vec3(1.0f, 1.0f, 0.7f);
            m.Emission      = glm::vec3(1.0f, 1.0f, 0.7f) * 5.0f;
            m.Roughness     = 1.0f;
            m.MaterialIndex = 4;
            m.LightIndex    = 0;
            scene.Materials.push_back(m);
        }
        // Index 5: AABB box material (white, subsurface)
        {
            Material m;
            m.Name          = "cb_aabb_box";
            m.Albedo        = glm::vec3(0.9f);
            m.Roughness     = 0.5f;
            m.Subsurface    = 1.0f;
            m.Eta           = 1.7f;
            m.Extinction    = glm::vec3(1.0f, 0.9f, 1.0f);
            m.MaterialIndex = 5;
            scene.Materials.push_back(m);
        }
        // Index 6: sphere material (chrome reflective)
        {
            Material m;
            m.Name          = "cb_sphere_chrome";
            m.Albedo        = glm::vec3(0.549f, 0.556f, 0.554f);  // chromium reflectance
            m.Metallic      = 1.0f;
            m.Roughness     = 0.0f;
            m.Extinction    = glm::vec3(0.7f, 1.0f, 1.0f);
            m.Eta           = 1.5f;
            m.MaterialIndex = 6;
            scene.Materials.push_back(m);
        }

        // Area Light
        {
            Light l;
            l.Position  = glm::vec3(0.0f, 4.799f, -1.5f);
            l.Emission  = glm::vec3(1.0f, 1.0f, 0.7f);
            l.Intensity = 5.0f;
            l.Size      = 1.825f;
            l.Type      = LightType::Square;
            scene.Lights.push_back(l);
        }

        const float x0 = -2.75f, x1 = 2.75f;
        const float y0 = -0.2f, y1 = 4.8f;
        const float z0 = -5.0f, z1 = 2.0f;

        // Meshes
        // Floor (mat 0)
        {
            Mesh m;
            m.Name          = "cb_floor_mesh";
            m.MaterialIndex = 0;
            m.Transform     = glm::mat4(1.0f);
            AddQuad(m, glm::vec3(x0, y0, z0), glm::vec3(x1, y0, z0), glm::vec3(x1, y0, z1), glm::vec3(x0, y0, z1),
                    glm::vec3(0, 1, 0), 0);
            scene.StaticMeshes.push_back(m);
        }
        // Ceiling (mat 1 white)
        {
            Mesh m;
            m.Name          = "cb_ceiling_mesh";
            m.MaterialIndex = 1;
            m.Transform     = glm::mat4(1.0f);
            AddQuad(m, glm::vec3(x0, y1, z1), glm::vec3(x1, y1, z1), glm::vec3(x1, y1, z0), glm::vec3(x0, y1, z0),
                    glm::vec3(0, -1, 0), 1);
            scene.StaticMeshes.push_back(m);
        }
        // Back wall (mat 1 white)
        {
            Mesh m;
            m.Name          = "cb_backwall_mesh";
            m.MaterialIndex = 1;
            m.Transform     = glm::mat4(1.0f);
            AddQuad(m, glm::vec3(x0, y0, z0), glm::vec3(x0, y1, z0), glm::vec3(x1, y1, z0), glm::vec3(x1, y0, z0),
                    glm::vec3(0, 0, 1), 1);
            scene.StaticMeshes.push_back(m);
        }
        // Left wall (green)
        {
            Mesh m;
            m.Name          = "cb_leftwall_mesh";
            m.MaterialIndex = 2;
            m.Transform     = glm::mat4(1.0f);
            AddQuad(m, glm::vec3(x0, y0, z1), glm::vec3(x0, y1, z1), glm::vec3(x0, y1, z0), glm::vec3(x0, y0, z0),
                    glm::vec3(1, 0, 0), 2);
            scene.StaticMeshes.push_back(m);
        }
        // Right wall (red)
        {
            Mesh m;
            m.Name          = "cb_rightwall_mesh";
            m.MaterialIndex = 3;
            m.Transform     = glm::mat4(1.0f);
            AddQuad(m, glm::vec3(x1, y0, z0), glm::vec3(x1, y1, z0), glm::vec3(x1, y1, z1), glm::vec3(x1, y0, z1),
                    glm::vec3(-1, 0, 0), 3);
            scene.StaticMeshes.push_back(m);
        }
        // Area light quad mesh
        {
            const float s = 1.825f * 0.5f;
            Mesh m;
            m.Name          = "cb_light_mesh";
            m.MaterialIndex = 4;
            m.Transform     = glm::mat4(1.0f);
            AddQuad(m, glm::vec3(-s, 4.799f, -1.5f - s), glm::vec3(s, 4.799f, -1.5f - s),
                    glm::vec3(s, 4.799f, -1.5f + s), glm::vec3(-s, 4.799f, -1.5f + s), glm::vec3(0, -1, 0), 4);
            scene.StaticMeshes.push_back(m);
        }

        // Procedural primitives
        // DieXaR Cornell Box
        {
            glm::vec3 center(0.9f, 1.3f, -2.9f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), center);
            glm::mat4 r = glm::rotate(glm::mat4(1.0f), -2.0f, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(0.9f, 1.5f, 0.9f));
            scene.ProceduralEntities.push_back(MakeAABB("cb_aabb", t * r * s, 5));
        }
        // Sphere
        {
            scene.ProceduralEntities.push_back(MakeSphere("cb_sphere", glm::vec3(-1.0f, 0.8f, -1.0f), 1.0f, 6));
        }

        return scene;
    }

    auto SceneFactory::CreateDemo(CameraHint* camHint) -> Scene
    {
        Scene scene;
        scene.SceneIndex              = SCENE_DEMO;
        scene.RaytracingType          = RaytracingMode::PathTracing;
        scene.ImportanceSampling      = ImportanceSamplingMode::BSDF;
        scene.MaxRecursionDepth       = 8;
        scene.MaxShadowRecursionDepth = scene.MaxRecursionDepth;
        scene.PathSqrtSamplesPerPixel = 1;
        scene.ApplyJitter             = true;
        scene.OnlyOneLightSample      = false;
        scene.RussianRouletteDepth    = 3;
        scene.AnisotropicBSDF         = true;
        scene.BackgroundColor         = glm::vec3(0.0f);

        if (camHint) {
            camHint->Eye    = glm::vec3(10.63f, 5.21f, 4.13f);
            camHint->Target = camHint->Eye + glm::vec3(-0.89f, -0.24f, -0.38f);
        }

        // Materials
        // 0: floor (grey)
        {
            Material m;
            m.Name          = "demo_floor";
            m.Albedo        = glm::vec3(0.9f);
            m.Roughness     = 0.4f;
            m.MaterialIndex = 0;
            scene.Materials.push_back(m);
        }
        // 1: AABB orange (transmission)
        {
            Material m;
            m.Name                 = "demo_aabb";
            m.Albedo               = glm::vec3(1.0f, 0.647f, 0.0f);
            m.Roughness            = 0.2f;
            m.SpecularTransmission = 0.7f;
            m.Eta                  = 1.7f;
            m.Subsurface           = 1.0f;
            m.Extinction           = glm::vec3(1.0f, 0.9f, 1.0f);
            m.MaterialIndex        = 1;
            scene.Materials.push_back(m);
        }
        // 2: sphere red (transmission)
        {
            Material m;
            m.Name                 = "demo_sphere";
            m.Albedo               = glm::vec3(0.8f, 0.1f, 0.1f);
            m.SpecularTransmission = 1.0f;
            m.Eta                  = 1.5f;
            m.Extinction           = glm::vec3(0.7f, 1.0f, 1.0f);
            m.MaterialIndex        = 2;
            scene.Materials.push_back(m);
        }
        // 3: IntersectedRoundCube green
        {
            Material m;
            m.Name          = "demo_round_cube";
            m.Albedo        = glm::vec3(0.2f, 0.8f, 0.2f);
            m.Roughness     = 0.8f;
            m.MaterialIndex = 3;
            scene.Materials.push_back(m);
        }
        // 4: SquareTorus violet
        {
            Material m;
            m.Name          = "demo_torus";
            m.Albedo        = glm::vec3(0.5f, 0.0f, 0.8f);
            m.Roughness     = 0.1f;
            m.SpecularTint  = 0.2f;
            m.Clearcoat     = 0.7f;
            m.MaterialIndex = 4;
            scene.Materials.push_back(m);
        }
        // 5: Cog yellow
        {
            Material m;
            m.Name          = "demo_cog";
            m.Albedo        = glm::vec3(1.0f, 0.9f, 0.0f);
            m.Roughness     = 0.9f;
            m.Eta           = 1.51f;
            m.MaterialIndex = 5;
            scene.Materials.push_back(m);
        }
        // 6: Cylinder silver (metallic)
        {
            Material m;
            m.Name          = "demo_cylinder";
            m.Albedo        = glm::vec3(0.75f, 0.75f, 0.75f);
            m.Metallic      = 1.0f;
            m.Roughness     = 0.0f;
            m.Eta           = 0.15f;
            m.MaterialIndex = 6;
            scene.Materials.push_back(m);
        }
        // 7: SolidAngle copper (metallic)
        {
            Material m;
            m.Name           = "demo_solid_angle";
            m.Albedo         = glm::vec3(0.72f, 0.45f, 0.2f);
            m.Metallic       = 1.0f;
            m.Roughness      = 0.0f;
            m.Clearcoat      = 0.7f;
            m.ClearcoatGloss = 1.0f;
            m.Eta            = 1.1f;
            m.MaterialIndex  = 7;
            scene.Materials.push_back(m);
        }

        // Lights
        {
            Light l;
            l.Position  = glm::vec3(-0.8f, 3.882f, 0.393f);
            l.Emission  = glm::vec3(0.474f, 0.376f, 0.75f);
            l.Intensity = 5.885f;
            l.Size      = 1.825f;
            l.Type      = LightType::Square;
            scene.Lights.push_back(l);
        }
        {
            Light l;
            l.Position  = glm::vec3(2.4f, 11.368f, -1.275f);
            l.Emission  = glm::vec3(0.78f, 0.815f, 0.65f);
            l.Intensity = 4.792f;
            l.Size      = 5.468f;
            l.Type      = LightType::Square;
            scene.Lights.push_back(l);
        }

        // Floor mesh
        {
            Mesh m;
            m.Name          = "demo_floor_mesh";
            m.MaterialIndex = 0;
            m.Transform     = glm::mat4(1.0f);
            const float S   = 30.0f;
            AddQuad(m, glm::vec3(-S, 0, -S), glm::vec3(S, 0, -S), glm::vec3(S, 0, S), glm::vec3(-S, 0, S),
                    glm::vec3(0, 1, 0), 0);
            scene.StaticMeshes.push_back(m);
        }

        // Procedural primitives
        // AABB
        {
            glm::vec3 center(4.8f, 1.5f, -5.0f);
            glm::vec3 half(1.0f, 1.5f, 1.0f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), center);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), half);
            scene.ProceduralEntities.push_back(MakeAABB("demo_aabb", t * s, 1));
        }
        // Sphere
        {
            scene.ProceduralEntities.push_back(MakeSphere("demo_sphere", glm::vec3(2.5f, 1.5f, -0.9f), 1.5f, 2));
        }

        // SDF primitives
        // IntersectedRoundCube
        {
            glm::vec3 c(-2.0f, 1.0f, -1.0f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), c);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
            ProceduralEntity pe;
            pe.Name          = "demo_round_cube";
            pe.IsAnalytic    = false;
            pe.PrimitiveType = static_cast<uint32_t>(SDFPrimitiveType::IntersectedRoundCube);
            pe.MaterialIndex = 3;
            pe.Transform     = t * s;
            scene.ProceduralEntities.push_back(pe);
        }
        // SquareTorus
        {
            glm::vec3 c(1.5f, 1.1f, 3.5f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), c);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));
            ProceduralEntity pe;
            pe.Name          = "demo_torus";
            pe.IsAnalytic    = false;
            pe.PrimitiveType = static_cast<uint32_t>(SDFPrimitiveType::SquareTorus);
            pe.MaterialIndex = 4;
            pe.Transform     = t * s;
            scene.ProceduralEntities.push_back(pe);
        }
        // Cog
        {
            glm::vec3 c(0.0f, 1.0f, -5.0f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), c);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
            ProceduralEntity pe;
            pe.Name          = "demo_cog";
            pe.IsAnalytic    = false;
            pe.PrimitiveType = static_cast<uint32_t>(SDFPrimitiveType::Cog);
            pe.MaterialIndex = 5;
            pe.Transform     = t * s;
            scene.ProceduralEntities.push_back(pe);
        }
        // Cylinder:
        {
            glm::vec3 c(-2.0f, 1.5f, 3.0f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), c);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.5f, 1.0f));
            ProceduralEntity pe;
            pe.Name          = "demo_cylinder";
            pe.IsAnalytic    = false;
            pe.PrimitiveType = static_cast<uint32_t>(SDFPrimitiveType::Cylinder);
            pe.MaterialIndex = 6;
            pe.Transform     = t * s;
            scene.ProceduralEntities.push_back(pe);
        }
        // SolidAngle
        {
            glm::vec3 c(6.0f, 5.0f, 3.0f);
            glm::mat4 t = glm::translate(glm::mat4(1.0f), c);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(5.0f));
            ProceduralEntity pe;
            pe.Name          = "demo_solid_angle";
            pe.IsAnalytic    = false;
            pe.PrimitiveType = static_cast<uint32_t>(SDFPrimitiveType::SolidAngle);
            pe.MaterialIndex = 7;
            pe.Transform     = t * s;
            scene.ProceduralEntities.push_back(pe);
        }

        return scene;
    }

    auto SceneFactory::CreatePbrShowcase(CameraHint* camHint) -> Scene
    {
        Scene scene;
        scene.SceneIndex              = SCENE_PBR_SHOWCASE;
        scene.RaytracingType          = RaytracingMode::PathTracing;
        scene.ImportanceSampling      = ImportanceSamplingMode::BSDF;
        scene.MaxRecursionDepth       = 8;
        scene.MaxShadowRecursionDepth = scene.MaxRecursionDepth;
        scene.PathSqrtSamplesPerPixel = 1;
        scene.ApplyJitter             = true;
        scene.OnlyOneLightSample      = false;
        scene.RussianRouletteDepth    = 3;
        scene.AnisotropicBSDF         = true;
        scene.BackgroundColor         = glm::vec3(0.0f);

        if (camHint) {
            camHint->Eye    = glm::vec3(-15.63f, 5.21f, 0.0f);
            camHint->Target = glm::vec3(0.0f, 0.0f, 0.0f);
        }

        // Directional light
        {
            Light l;
            l.Position  = glm::vec3(0.0f, 18.0f, -20.0f);
            l.Emission  = glm::vec3(0.8f, 0.8f, 0.65f);
            l.Intensity = 1.0f;
            l.Direction = glm::normalize(glm::vec3(0.76f, -0.196f, 0.596f));
            l.Type      = LightType::Directional;
            scene.Lights.push_back(l);
        }

        std::mt19937 rng(67);  // rng for material properties
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        // Material 0: floor
        {
            Material m;
            m.Name          = "pbr_floor";
            m.Albedo        = glm::vec3(0.75f);
            m.Roughness     = 0.4f;
            m.MaterialIndex = 0;
            scene.Materials.push_back(m);
        }

        // Materials 1..numSpheres for spheres
        const uint32_t numSpheres = 40;
        for (uint32_t i = 0; i < numSpheres; ++i) {
            Material m;
            m.Name                 = "pbr_sphere_" + std::to_string(i);
            m.Albedo               = glm::vec3(dist(rng), dist(rng), dist(rng));
            m.Anisotropic          = dist(rng) > 0.5f ? 1.0f : 0.0f;
            m.Metallic             = dist(rng);
            m.Roughness            = dist(rng);
            m.Sheen                = dist(rng);
            m.SheenTint            = dist(rng);
            m.SpecularTint         = dist(rng);
            m.SpecularTransmission = dist(rng) > 0.5f ? 1.0f : 0.0f;
            m.Subsurface           = dist(rng);
            m.Clearcoat            = dist(rng);
            m.ClearcoatGloss       = dist(rng);
            const float emitChance = dist(rng);
            if (emitChance > 0.9f) m.Emission = glm::vec3(dist(rng), dist(rng), dist(rng));
            m.MaterialIndex = i + 1;
            scene.Materials.push_back(m);
        }

        // Floor mesh
        {
            Mesh m;
            m.Name          = "pbr_floor_mesh";
            m.MaterialIndex = 0;
            m.Transform     = glm::mat4(1.0f);
            const float S   = 50.0f;
            AddQuad(m, glm::vec3(-S, 0, -S), glm::vec3(S, 0, -S), glm::vec3(S, 0, S), glm::vec3(-S, 0, S),
                    glm::vec3(0, 1, 0), 0);
            scene.StaticMeshes.push_back(m);
        }

        // Sphere grid
        const float tlas_x = 4.0f, tlas_y = 0.8f, tlas_z = 1.0f;
        const float base_x = -9.0f, base_y = -1.0f, base_z = -9.0f;
        const float stride = 2.0f;
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 8; ++j) {
                int idx  = i * 8 + j;
                float cx = base_x + i * stride + 1.0f;
                float cy = base_y + 1.0f;
                float cz = base_z + j * stride + 1.0f;
                float wx = cx + tlas_x;
                float wy = cy + tlas_y;
                float wz = cz + tlas_z;
                scene.ProceduralEntities.push_back(
                        MakeSphere("pbr_sphere_" + std::to_string(idx), glm::vec3(wx, wy, wz), 1.0f, idx + 1));
            }
        }

        return scene;
    }

    void SceneFactory::AddQuad(Mesh& mesh, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
            const glm::vec3& p3, const glm::vec3& normal, int matIdx)
    {
        const uint32_t base = (uint32_t) mesh.Vertices.size();
        glm::vec2 uv[4]     = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        mesh.Vertices.push_back({ p0, normal, uv[0] });
        mesh.Vertices.push_back({ p1, normal, uv[1] });
        mesh.Vertices.push_back({ p2, normal, uv[2] });
        mesh.Vertices.push_back({ p3, normal, uv[3] });
        mesh.Indices.push_back(base + 0);
        mesh.Indices.push_back(base + 1);
        mesh.Indices.push_back(base + 2);
        mesh.Indices.push_back(base + 0);
        mesh.Indices.push_back(base + 2);
        mesh.Indices.push_back(base + 3);
    }

    auto SceneFactory::MakeSphere(const std::string& name, const glm::vec3& center, float radius, int materialIndex)
            -> ProceduralEntity
    {
        ProceduralEntity pe;
        pe.Name          = name;
        pe.IsAnalytic    = true;
        pe.PrimitiveType = static_cast<uint32_t>(AnalyticPrimitiveType::Sphere);
        pe.MaterialIndex = materialIndex;
        glm::mat4 t      = glm::translate(glm::mat4(1.0f), center);
        glm::mat4 s      = glm::scale(glm::mat4(1.0f), glm::vec3(radius));
        pe.Transform     = t * s;
        return pe;
    }

    auto SceneFactory::MakeAABB(const std::string& name, const glm::mat4& transform, int materialIndex)
            -> ProceduralEntity
    {
        ProceduralEntity pe;
        pe.Name          = name;
        pe.IsAnalytic    = true;
        pe.PrimitiveType = static_cast<uint32_t>(AnalyticPrimitiveType::AABB);
        pe.MaterialIndex = materialIndex;
        pe.Transform     = transform;
        return pe;
    }
}  // namespace Vlkrt
