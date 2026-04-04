import hou
import viewerstate.utils as su

CANVAS_PATH = "/obj/canvas_geo" # Default.
STROKE_PATH = "/obj/geo1/stroke_points"

class GaussianPaintState:
    def __init__(self, state_name, scene_viewer):
        self.state_name   = state_name
        self.scene_viewer = scene_viewer
        self.is_drawing   = False
        self.last_pos     = None
        self.hit_points   = []
        self.all_strokes  = []
        self.initialized  = False
        self.canvas_path  = getattr(hou.session, 'gaussian_paint_canvas_path', None) # New, for base surface input from HDK plugin.
        self.screen_cache = []
        print(f"[GaussianPaint] Initialized with canvas path: {self.canvas_path}") # Test.

    # Instead of raycasting against .ply, find nearest .ply point matching to screenspace location of mouse.
    def _screen_space_nearest(self, mouse_x, mouse_y):
        if self.canvas_path:
            target_path = self.canvas_path
        else:
            target_path = CANVAS_PATH

        canvas = hou.node(target_path)
        if canvas is None:
            return None, None

        geo      = canvas.displayNode().geometry()
        viewport = self.scene_viewer.curViewport()

        SCREEN_RADIUS_PX = 50

        best_screen_dist = float('inf')
        best_pt          = None

        for screen_pos, pt in self.screen_cache:
            dx = screen_pos[0] - mouse_x
            dy = screen_pos[1] - mouse_y
            screen_dist = (dx*dx + dy*dy) ** 0.5

            if screen_dist < SCREEN_RADIUS_PX and screen_dist < best_screen_dist:
                best_screen_dist = screen_dist
                best_pt          = pt

        if best_pt is None:
            return None, None

        best_pos = best_pt.position()

        try:
            n = hou.Vector3(best_pt.attribValue("N"))
            if n.length() > 1e-6:
                return best_pos, n.normalized()
        except:
            pass
        
        geo  = best_pt.geometry()
        norm = self._estimate_normal_pca(geo, best_pos, k=8)
        return best_pos, norm

    def _raycast(self, mouse_x, mouse_y):
        if self.canvas_path:
            target_path = self.canvas_path
        else:
            target_path = CANVAS_PATH
        
        # Checking if we can actually read input 2 for surface input from HDK plugin.
        canvas = hou.node(target_path)
        if canvas is None:
            print(f"[GaussianPaint] Could not find node: {target_path}")
            return None, None
        
        viewport = self.scene_viewer.curViewport()
        origin, direction = viewport.mapToWorld(mouse_x, mouse_y)

        geo  = canvas.displayNode().geometry()
        if target_path == CANVAS_PATH:
            viewport = self.scene_viewer.curViewport()
            origin, direction = viewport.mapToWorld(mouse_x, mouse_y)
            pos  = hou.Vector3()
            norm = hou.Vector3()
            uvw  = hou.Vector3()
            hit  = geo.intersect(hou.Vector3(origin), hou.Vector3(direction), pos, norm, uvw)
            if hit >= 0:
                return hou.Vector3(pos), hou.Vector3(norm).normalized()
            return None, None
        
        return self._screen_space_nearest(mouse_x, mouse_y)

    def _raycast_pointcloud(self, geo, origin, direction):
        """
        Projects the camera ray against all points and finds the nearest one.
        Returns (position, normal) of the closest .ply point to the ray.
        """
        best_t    = -1.0
        best_dist = float('inf')
        best_pt   = None

        candidates = []
        for pt in geo.points():
            p  = pt.position()
            op = p - origin
            t  = op.dot(direction)
            if t < 0:
                continue 
            closest  = origin + direction * t
            dist_ray = (p - closest).length()
            candidates.append((dist_ray, t, pt))

        candidates.sort(key=lambda x: x[0])

        for dist_ray, t, pt in candidates:
            if dist_ray > 0.5: 
                break

            pos = pt.position()

            if self.last_pos is not None:
                if (pos - self.last_pos).length() < 0.001:
                    continue

            best_dist = dist_ray
            best_pt   = pt
            break
        
        if best_pt is None:
            print(f"[GaussianPaint] No hit — best_dist={best_dist:.4f}")
            return None, None

        best_pos = best_pt.position()
        print(f"[GaussianPaint] Hit splat at dist={best_dist:.4f}, pos={best_pos}")

        # Splats do not have normals, they have orientation and position.
        # Can try to read orientation + position, calculate normal from there.
        try:
            n = hou.Vector3(best_pt.attribValue("N"))
            if n.length() > 1e-6:
                return best_pos, n.normalized()
        except:
            pass

        norm = self._estimate_normal_pca(geo, best_pos, k=8)
        return best_pos, norm


    def _estimate_normal_pca(self, geo, center, k=8):
        """
        Estimate surface normal at a point by fitting a plane to the k nearest neighbours using PCA on the covariance matrix.
        Flips the result to face the camera.
        """
        import math

        center = hou.Vector3(center)

        pts_and_dists = []
        for pt in geo.points():
            p    = pt.position()
            diff = p - center
            d    = diff.length()
            pts_and_dists.append((d, p))

        pts_and_dists.sort(key=lambda x: x[0])
        neighbours = [p for _, p in pts_and_dists[1:k+1]]  # skip index 0 (self)

        if len(neighbours) < 3:
            return hou.Vector3(0, 1, 0)

        cx = sum(p[0] for p in neighbours) / len(neighbours)
        cy = sum(p[1] for p in neighbours) / len(neighbours)
        cz = sum(p[2] for p in neighbours) / len(neighbours)

        cov = [[0.0]*3 for _ in range(3)]
        for p in neighbours:
            d = [p[0]-cx, p[1]-cy, p[2]-cz]
            for i in range(3):
                for j in range(3):
                    cov[i][j] += d[i] * d[j]

        def mat_vec(m, v):
            return [sum(m[i][j]*v[j] for j in range(3)) for i in range(3)]

        def normalize3(v):
            l = math.sqrt(sum(x*x for x in v))
            return [x/l for x in v] if l > 1e-8 else [0, 1, 0]

        trace = cov[0][0] + cov[1][1] + cov[2][2]
        shifted = [[-cov[i][j] + (trace if i==j else 0) for j in range(3)] for i in range(3)]

        vec = [1.0, 0.0, 0.0]
        for _ in range(32): 
            vec = mat_vec(shifted, vec)
            vec = normalize3(vec)

        cam_pos = hou.Vector3(
            self.scene_viewer.curViewport().viewTransform().extractTranslates()
        )
        to_cam = cam_pos - center
        if sum(vec[i] * to_cam[i] for i in range(3)) < 0:
            vec = [-x for x in vec]

        return hou.Vector3(vec[0], vec[1], vec[2]).normalized()

    def _flush_to_sop(self, event="active"):
        node = hou.node(STROKE_PATH)
        if node is None:
            return
        all_strokes    = self.all_strokes + ([self.hit_points] if self.hit_points else [])
        all_pts        = [(p, n) for stroke in all_strokes for p, n in stroke]
        stroke_lengths = [len(s) for s in all_strokes]
        pos_list       = [tuple(p) for p, n in all_pts]
        norm_list      = [tuple(n) for p, n in all_pts]

        event_map = {"begin": 1, "active": 2, "end": 3}
        event_int = event_map.get(event, 2)

        node.parm("point_positions").set(str(pos_list))
        node.parm("point_normals").set(str(norm_list))
        node.parm("stroke_lengths").set(str(stroke_lengths))
        node.parm("stroke_event").set(str(event_int))
        node.cook(force=True)

    def _check_external_reset(self):
        """If SOP was cleared externally (by Paint Stroke button), reset state."""
        node = hou.node(STROKE_PATH)
        if node is None:
            return
        pos_str = node.parm("point_positions").evalAsString()
        if (not pos_str or pos_str == "[]") and (self.all_strokes or self.hit_points):
            print("[GaussianPaint] Detected external reset — clearing state.")
            self.all_strokes = []
            self.hit_points  = []
            self.last_pos    = None
            self.is_drawing  = False

    def _cache_screen_positions(self, geo, viewport):
        self.screen_cache = []
        for pt in geo.points():
            screen_pos = viewport.mapToScreen(pt.position())
            self.screen_cache.append((screen_pos, pt))
        print(f"[GaussianPaint] Cached {len(self.screen_cache)} screen positions.")

    def onMouseEvent(self, kwargs):
        ui_event = kwargs["ui_event"]
        device   = ui_event.device()

        # check for external reset every mouse event
        self._check_external_reset()

        if not device.isLeftButton():
            if self.is_drawing:
                self.is_drawing = False
                self.last_pos   = None
                self.screen_cache = []
                self.all_strokes.append(self.hit_points)
                self.hit_points = []
                self._flush_to_sop(event="end")
                print(f"[GaussianPaint] Stroke complete. {len(self.all_strokes)} strokes total.")
            return False

        self.is_drawing = True

        if len(self.hit_points) == 0:
            target_path = self.canvas_path if self.canvas_path else CANVAS_PATH
            canvas      = hou.node(target_path)
            if canvas is not None and target_path != CANVAS_PATH:
                geo      = canvas.displayNode().geometry()
                viewport = self.scene_viewer.curViewport()
                self._cache_screen_positions(geo, viewport)
            self._flush_to_sop(event="begin")

        pos, norm = self._raycast(device.mouseX(), device.mouseY())
        if pos is None:
            return False

        viewport = self.scene_viewer.curViewport()
        cam_transform = viewport.viewTransform()
        cam_pos = hou.Vector3(cam_transform.extractTranslates())
        dist_to_surface = (cam_pos - pos).length()
        min_dist = dist_to_surface * 0.01

        if self.last_pos is not None:
            if (pos - self.last_pos).length() < min_dist:
                return True

        if len(self.hit_points) == 0:
            self._flush_to_sop(event="begin")

        self.last_pos = pos
        self.hit_points.append((pos, norm))
        self._flush_to_sop(event="active") 
        return True

    def onExit(self, kwargs):
        if self.hit_points:
            self.all_strokes.append(self.hit_points)
            self.hit_points = []
            self._flush_to_sop()
        print("[GaussianPaint] Exited.")


def createViewerStateTemplate():
    state_typename = "gaussian_paint"
    state_label    = "Gaussian Paint"
    state_cat      = hou.sopNodeTypeCategory()
    template = hou.ViewerStateTemplate(state_typename, state_label, state_cat)
    template.bindFactory(GaussianPaintState)
    return template