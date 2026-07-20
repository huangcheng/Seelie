#include "GltfLoader.h"

#include <QFileInfo>
#include <QDebug>
#include <QSet>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace {

QMatrix4x4 toMat4(const cgltf_float m[16])
{
    // cgltf stores matrices column-major. Read element (row, col) from
    // flat array index [col * 4 + row] via operator()(row, col).
    QMatrix4x4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r(row, col) = m[col * 4 + row];
    return r;
}

bool readAccessorFloats(const cgltf_accessor *acc, QVector<float> &out)
{
    const cgltf_size ncomp = cgltf_num_components(acc->type);
    out.resize(int(acc->count) * int(ncomp));
    for (cgltf_size i = 0; i < acc->count; ++i) {
        if (!cgltf_accessor_read_float(acc, i, out.data() + i * ncomp, ncomp))
            return false;
    }
    return true;
}

// Compose a node's local TRS into a 4x4 (T * R * S, same convention as the
// evaluator's hierarchy walk). glTF also permits a direct matrix; unused by
// the robot but supported for completeness.
QMatrix4x4 nodeLocalMatrix(const cgltf_node *n)
{
    QMatrix4x4 m;
    if (n->has_matrix) {
        m = toMat4(n->matrix);
        return m;
    }
    if (n->has_translation)
        m.translate(QVector3D(n->translation[0], n->translation[1], n->translation[2]));
    if (n->has_rotation)
        m.rotate(QQuaternion(n->rotation[3], n->rotation[0],
                             n->rotation[1], n->rotation[2]));
    if (n->has_scale)
        m.scale(QVector3D(n->scale[0], n->scale[1], n->scale[2]));
    return m;
}

} // namespace

bool GltfLoader::loadFromFile(const QString &path, Model3DModel &out, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };
    if (!QFileInfo::exists(path))
        return fail(QStringLiteral("GLB not found: %1").arg(path));

    cgltf_options options = {};
    cgltf_data *data = nullptr;
    const QByteArray utf8 = QFileInfo(path).absoluteFilePath().toUtf8();
    cgltf_result r = cgltf_parse_file(&options, utf8.constData(), &data);
    if (r != cgltf_result_success)
        return fail(QStringLiteral("cgltf parse failed (%1)").arg(int(r)));
    if (cgltf_load_buffers(&options, data, utf8.constData()) != cgltf_result_success) {
        cgltf_free(data);
        return fail(QStringLiteral("cgltf buffer load failed"));
    }
    if (cgltf_validate(data) != cgltf_result_success) {
        cgltf_free(data);
        return fail(QStringLiteral("cgltf validation failed"));
    }

    // --- Materials / textures (QImage decodes embedded PNG/JPEG) ---
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material &mat = data->materials[i];
        Model3DMaterial m;
        m.unlit = mat.unlit;
        const cgltf_texture *tex = mat.has_pbr_metallic_roughness
            ? mat.pbr_metallic_roughness.base_color_texture.texture : nullptr;
        if (tex && tex->image && tex->image->buffer_view) {
            const cgltf_buffer_view *bv = tex->image->buffer_view;
            const auto *bytes = static_cast<const uchar *>(bv->buffer->data) + bv->offset;
            m.baseColor = QImage::fromData(bytes, int(bv->size)).convertToFormat(QImage::Format_RGBA8888);
        }
        out.materials.append(m);
    }

    // --- Skeleton (first skin; v1 renders single-character packs) ---
    // Built BEFORE meshes so the jointOf map is available for rigid
    // primitive attachment lookup.
    QHash<const cgltf_node *, int> jointOf;
    if (data->skins_count > 0) {
        const cgltf_skin &skin = data->skins[0];
        const cgltf_size nj = skin.joints_count;
        out.joints.resize(int(nj));
        for (cgltf_size j = 0; j < nj; ++j) jointOf.insert(skin.joints[j], int(j));
        QVector<float> ibm;
        if (skin.inverse_bind_matrices &&
            !readAccessorFloats(skin.inverse_bind_matrices, ibm)) {
            cgltf_free(data);
            return fail(QStringLiteral("failed reading inverse bind matrices"));
        }
        for (cgltf_size j = 0; j < nj; ++j) {
            const cgltf_node *node = skin.joints[j];
            Model3DJoint &jj = out.joints[int(j)];
            jj.name = node->name ? QString::fromUtf8(node->name) : QStringLiteral("joint%1").arg(j);
            jj.parent = -1;
            // Parent = nearest ancestor that is also a joint.
            for (const cgltf_node *p = node->parent; p; p = p->parent) {
                if (jointOf.contains(p)) { jj.parent = jointOf.value(p); break; }
            }
            if (node->has_translation)
                jj.bindT = QVector3D(node->translation[0], node->translation[1], node->translation[2]);
            if (node->has_rotation)
                jj.bindR = QQuaternion(node->rotation[3], node->rotation[0],
                                       node->rotation[1], node->rotation[2]);
            if (node->has_scale)
                jj.bindS = QVector3D(node->scale[0], node->scale[1], node->scale[2]);
            if (skin.inverse_bind_matrices)
                jj.inverseBind = toMat4(ibm.constData() + j * 16);

            // preTransform: product of NON-JOINT ancestor node transforms
            // between this joint and its nearest JOINT ancestor (exclusive).
            // For the robot, Bone's preTransform = RootNode × RobotArmature
            // = scale(100) × rotX(-90°). Without this, joint globals are
            // armature-local while IBMs are world-inverse, so the bind-pose
            // palette equals armature⁻¹ and the model renders at 1/100 scale.
            // Collect non-joint ancestors in top-down order (root first).
            QVector<const cgltf_node *> chain;
            for (const cgltf_node *p = node->parent; p; p = p->parent) {
                if (jointOf.contains(p)) break;  // stop at nearest JOINT ancestor
                chain.prepend(p);
            }
            QMatrix4x4 pre;
            for (const cgltf_node *n : chain)
                pre *= nodeLocalMatrix(n);
            jj.preTransform = pre;
        }
    }

    // --- Mesh primitives (node-aware) ---
    // Iterate data->nodes (not data->meshes) so each primitive carries its
    // referencing node's transform context. A mesh referenced by multiple
    // nodes is processed exactly once — v1 takes the first referencing node.
    QSet<const cgltf_mesh *> processedMeshes;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node *node = data->nodes + ni;
        if (!node->mesh) continue;
        if (processedMeshes.contains(node->mesh)) continue;
        processedMeshes.insert(node->mesh);

        // Per-node attachment info (computed once, applied to every primitive
        // of this mesh).
        const bool nodeSkinned = (node->skin != nullptr);
        int attachedJoint = -1;
        QMatrix4x4 attachTransform;
        if (!nodeSkinned) {
            // Rigid: find nearest joint ancestor by walking up parents.
            const cgltf_node *jointAncestor = nullptr;
            for (const cgltf_node *p = node; p; p = p->parent) {
                if (jointOf.contains(p)) { jointAncestor = p; break; }
            }
            if (jointAncestor) {
                attachedJoint = jointOf.value(jointAncestor);
                // Chain from jointAncestor (exclusive) down to node (inclusive),
                // top-down product. Walk up collecting, then reverse.
                QVector<const cgltf_node *> chain;
                for (const cgltf_node *p = node; p && p != jointAncestor; p = p->parent)
                    chain.prepend(p);
                for (const cgltf_node *n : chain)
                    attachTransform *= nodeLocalMatrix(n);
            } else {
                // No joint ancestor: full chain from scene root down to node.
                QVector<const cgltf_node *> chain;
                for (const cgltf_node *p = node; p; p = p->parent)
                    chain.prepend(p);
                for (const cgltf_node *n : chain)
                    attachTransform *= nodeLocalMatrix(n);
            }
        }

        const cgltf_mesh &mesh = *node->mesh;
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive &prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            Model3DPrimitive p;
            p.material = prim.material ? int(prim.material - data->materials) : -1;
            p.skinned = nodeSkinned;
            p.attachedJoint = attachedJoint;
            p.attachTransform = attachTransform;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr,
                                 *jnt = nullptr, *wgt = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute &a = prim.attributes[ai];
                switch (a.type) {
                case cgltf_attribute_type_position: pos = a.data; break;
                case cgltf_attribute_type_normal:   nrm = a.data; break;
                case cgltf_attribute_type_texcoord: if (!uv) uv = a.data; break;
                case cgltf_attribute_type_joints:   if (!jnt) jnt = a.data; break;
                case cgltf_attribute_type_weights:  if (!wgt) wgt = a.data; break;
                default: break;
                }
            }
            if (!pos) continue;
            const cgltf_size n = pos->count;
            p.vertices.resize(int(n));
            for (cgltf_size vi = 0; vi < n; ++vi) {
                Model3DVertex v = {};
                cgltf_accessor_read_float(pos, vi, &v.px, 3);
                if (nrm) cgltf_accessor_read_float(nrm, vi, &v.nx, 3);
                else { v.ny = 1.0f; }
                if (uv) cgltf_accessor_read_float(uv, vi, &v.u, 2);
                if (jnt) {
                    cgltf_uint j[4] = {0,0,0,0};
                    cgltf_accessor_read_uint(jnt, vi, j, 4);
                    for (int k = 0; k < 4; ++k) v.joints[k] = uint8_t(qMin(j[k], 255u));
                }
                if (wgt) cgltf_accessor_read_float(wgt, vi, v.weights, 4);
                else v.weights[0] = 1.0f;
                // Normalize weights (exporters occasionally drift from sum=1).
                const float sum = v.weights[0]+v.weights[1]+v.weights[2]+v.weights[3];
                if (sum > 1e-6f) for (int k = 0; k < 4; ++k) v.weights[k] /= sum;
                p.vertices[int(vi)] = v;
            }
            if (prim.indices) {
                p.indices.resize(int(prim.indices->count));
                for (cgltf_size ii = 0; ii < prim.indices->count; ++ii)
                    p.indices[int(ii)] = uint32_t(cgltf_accessor_read_index(prim.indices, ii));
            } else {
                p.indices.resize(int(n));
                for (cgltf_size ii = 0; ii < n; ++ii) p.indices[int(ii)] = uint32_t(ii);
            }
            out.primitives.append(p);
        }
    }

    // --- Animation clips ---
    for (cgltf_size ci = 0; ci < data->animations_count; ++ci) {
        const cgltf_animation &anim = data->animations[ci];
        Model3DClip clip;
        clip.name = anim.name ? QString::fromUtf8(anim.name)
                              : QStringLiteral("clip%1").arg(ci);
        for (cgltf_size chi = 0; chi < anim.channels_count; ++chi) {
            const cgltf_animation_channel &ch = anim.channels[chi];
            if (!ch.target_node || !ch.sampler) continue;
            // Only keep channels that target a skin joint.
            int joint = -1;
            for (cgltf_size j = 0; j < (data->skins_count ? data->skins[0].joints_count : 0); ++j) {
                if (data->skins[0].joints[j] == ch.target_node) { joint = int(j); break; }
            }
            if (joint < 0) continue;
            Model3DTrack t;
            t.joint = joint;
            int comps = 3;
            switch (ch.target_path) {
            case cgltf_animation_path_type_translation: t.path = Model3DTrack::Translation; break;
            case cgltf_animation_path_type_rotation:    t.path = Model3DTrack::Rotation; comps = 4; break;
            case cgltf_animation_path_type_scale:       t.path = Model3DTrack::Scale; break;
            default: continue; // weights (morphs) deferred — non-goal v1
            }
            if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
                qWarning() << "Model3D: clip" << clip.name
                           << "uses cubic-spline interpolation; sampled as linear (v1)";
            }
            if (!readAccessorFloats(ch.sampler->input, t.times)) continue;
            QVector<float> raw;
            if (!readAccessorFloats(ch.sampler->output, raw)) continue;
            // Cubic spline output has 3 values per key (in-tangent, value,
            // out-tangent); keep only the middle value.
            const int nkeys = int(ch.sampler->input->count);
            const bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            t.values.resize(nkeys * comps);
            for (int k = 0; k < nkeys; ++k)
                for (int c = 0; c < comps; ++c)
                    t.values[k*comps+c] = raw[(cubic ? k*3+1 : k) * comps + c];
            if (!t.times.isEmpty())
                clip.duration = qMax(clip.duration, t.times.last());
            clip.tracks.append(t);
        }
        out.clipIndexByName.insert(clip.name, out.clips.size());
        out.clips.append(clip);
    }

    cgltf_free(data);
    return true;
}
