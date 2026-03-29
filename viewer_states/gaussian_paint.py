import hou
import viewerstate.utils as su

CANVAS_PATH = "/obj/canvas_geo"
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

    def _raycast(self, mouse_x, mouse_y):
        viewport = self.scene_viewer.curViewport()
        origin, direction = viewport.mapToWorld(mouse_x, mouse_y)
        canvas = hou.node(CANVAS_PATH)
        if canvas is None:
            return None, None
        geo  = canvas.displayNode().geometry()
        pos  = hou.Vector3()
        norm = hou.Vector3()
        uvw  = hou.Vector3()
        hit  = geo.intersect(hou.Vector3(origin), hou.Vector3(direction), pos, norm, uvw)
        if hit < 0:
            return None, None
        return hou.Vector3(pos), hou.Vector3(norm).normalized()

    def _flush_to_sop(self):
        node = hou.node(STROKE_PATH)
        if node is None:
            return
        all_strokes = self.all_strokes + ([self.hit_points] if self.hit_points else [])
        all_pts = [(p, n) for stroke in all_strokes for p, n in stroke]
        stroke_lengths = [len(s) for s in all_strokes]
        pos_list  = [tuple(p) for p, n in all_pts]
        norm_list = [tuple(n) for p, n in all_pts]
        node.parm("point_positions").set(str(pos_list))
        node.parm("point_normals").set(str(norm_list))
        node.parm("stroke_lengths").set(str(stroke_lengths))
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

    def onMouseEvent(self, kwargs):
        ui_event = kwargs["ui_event"]
        device   = ui_event.device()

        # check for external reset every mouse event
        self._check_external_reset()

        if not device.isLeftButton():
            if self.is_drawing:
                self.is_drawing = False
                self.last_pos   = None
                self.all_strokes.append(self.hit_points)
                self.hit_points = []
                self._flush_to_sop()
                print(f"[GaussianPaint] Stroke complete. {len(self.all_strokes)} strokes total.")
            return False

        self.is_drawing = True
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

        self.last_pos = pos
        self.hit_points.append((pos, norm))
        self._flush_to_sop()
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