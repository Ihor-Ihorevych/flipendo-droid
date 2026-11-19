package io.github.flipendo.spike;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.Xml;

import org.xmlpull.v1.XmlPullParser;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * A tiny SVG renderer for the PS2 pad's art (android/assets/ps2pad/*.svg): the subset the art uses, parsed once into Paths and Paints, drawn
 * with a Canvas without allocating. Supported: svg viewBox; g (id = a layer, transform, inherited presentation attributes); path (M L H V C S Q
 * A Z, absolute and relative; arcs without x-axis rotation), circle, ellipse, rect (rx ry), line, polygon, polyline; fill and stroke as colours
 * (#rgb #rrggbb, a few names, none, currentColor = the tint given to draw()) or url(#gradient); linearGradient and radialGradient (stops with
 * stop-color and stop-opacity, objectBoundingBox or userSpaceOnUse); stroke-width, stroke-linecap, stroke-linejoin, opacity, fill-opacity,
 * stroke-opacity, vector-effect="non-scaling-stroke"; the style="" attribute. Text, filters, clip paths, use and gradientTransform are not supported.
 * <p>
 * The shapes outside any g with an id are the layer "base"; a g with an id puts its shapes in the layer of that name, which the caller draws
 * when it wants it (the pad's "glow" and "lit" layers are the pressed state). Nothing is clipped, so a layer may reach outside the viewBox.
 */
final class Ps2Svg {
    private static final HashMap<String, Ps2Svg> CACHE = new HashMap<String, Ps2Svg>();

    /** The art called name (assets/ps2pad/name.svg), parsed once. When it cannot be read, ok is false and drawing does nothing. */
    static synchronized Ps2Svg get(Context context, String name) {
        Ps2Svg s = CACHE.get(name);
        if (s == null) {
            s = new Ps2Svg();
            InputStream in = null;
            try {
                in = context.getAssets().open("ps2pad/" + name + ".svg");
                s.parse(in);
                s.ok = true;
            } catch (Exception e) {
                android.util.Log.w("Ps2Svg", name + ": " + e);
            } finally {
                try {
                    if (in != null) in.close();
                } catch (Exception ignored) {
                }
            }
            s.finish();
            CACHE.put(name, s);
        }
        return s;
    }

    boolean ok;
    /** The viewBox: the art's own coordinate frame, mapped onto the rectangle given to draw(). */
    final RectF view = new RectF(0, 0, 100, 100);

    private static final class Shape {
        Path path;
        Paint fill, stroke;
        int fillA, strokeA;
        boolean fillTint, strokeTint;
        boolean fixed;          // vector-effect="non-scaling-stroke": the stroke width is in screen pixels
        float strokeW;
        String layer;
    }

    private static final Shape[] NONE = new Shape[0];
    private final ArrayList<Shape> shapes = new ArrayList<Shape>();
    private final HashMap<String, Shape[]> layers = new HashMap<String, Shape[]>();

    /** Draws one layer of the art into dst (uniformly scaled, centred). alpha multiplies every shape's own alpha, 256 = as drawn (up to 512, shapes cap at opaque); tint is what currentColor means. */
    void draw(Canvas c, RectF dst, String layer, int alpha, int tint) {
        Shape[] list = layers.get(layer);
        if (list == null || alpha <= 0) return;
        float s = Math.min(dst.width() / view.width(), dst.height() / view.height());
        c.save();
        c.translate(dst.centerX() - view.centerX() * s, dst.centerY() - view.centerY() * s);
        c.scale(s, s);
        for (Shape sh : list) {
            if (sh.fill != null) {
                int a = Math.min(255, (sh.fillA * alpha) >> 8);
                if (sh.fillTint) sh.fill.setColor((a << 24) | (tint & 0xFFFFFF)); else sh.fill.setAlpha(a);
                c.drawPath(sh.path, sh.fill);
            }
            if (sh.stroke != null) {
                int a = Math.min(255, (sh.strokeA * alpha) >> 8);
                if (sh.fixed) sh.stroke.setStrokeWidth(sh.strokeW / s);
                if (sh.strokeTint) sh.stroke.setColor((a << 24) | (tint & 0xFFFFFF)); else sh.stroke.setAlpha(a);
                c.drawPath(sh.path, sh.stroke);
            }
        }
        c.restore();
    }

    private void finish() {
        HashMap<String, ArrayList<Shape>> by = new HashMap<String, ArrayList<Shape>>();
        for (Shape sh : shapes) {
            ArrayList<Shape> l = by.get(sh.layer);
            if (l == null) {
                l = new ArrayList<Shape>();
                by.put(sh.layer, l);
            }
            l.add(sh);
        }
        for (java.util.Map.Entry<String, ArrayList<Shape>> e : by.entrySet()) layers.put(e.getKey(), e.getValue().toArray(NONE));
    }

    // ---- parsing -------------------------------------------------------------------------------------------------------

    private static final class Style {
        String fill = "black", stroke = "none", cap = "butt", join = "miter", layer = "base";
        boolean fixed;
        float strokeWidth = 1f, opacity = 1f, fillOpacity = 1f, strokeOpacity = 1f;
        Matrix m = new Matrix();

        Style copy() {
            Style s = new Style();
            s.fill = fill;
            s.stroke = stroke;
            s.cap = cap;
            s.join = join;
            s.layer = layer;
            s.fixed = fixed;
            s.strokeWidth = strokeWidth;
            s.opacity = opacity;
            s.fillOpacity = fillOpacity;
            s.strokeOpacity = strokeOpacity;
            s.m = new Matrix(m);
            return s;
        }

        void apply(HashMap<String, String> at, boolean group) {
            String v;
            if ((v = at.get("fill")) != null) fill = v;
            if ((v = at.get("stroke")) != null) stroke = v;
            if ((v = at.get("stroke-linecap")) != null) cap = v;
            if ((v = at.get("stroke-linejoin")) != null) join = v;
            if ((v = at.get("stroke-width")) != null) strokeWidth = num(v, strokeWidth);
            if ((v = at.get("vector-effect")) != null) fixed = v.equals("non-scaling-stroke");
            if ((v = at.get("opacity")) != null) opacity *= num(v, 1f);
            if ((v = at.get("fill-opacity")) != null) fillOpacity = num(v, 1f);
            if ((v = at.get("stroke-opacity")) != null) strokeOpacity = num(v, 1f);
            if (group && (v = at.get("id")) != null) layer = v;
            if ((v = at.get("transform")) != null) m.preConcat(transform(v));
        }
    }

    private static final class Grad {
        boolean linear;
        HashMap<String, String> at;
        final ArrayList<Float> offs = new ArrayList<Float>();
        final ArrayList<Integer> cols = new ArrayList<Integer>();
    }

    private final HashMap<String, Grad> grads = new HashMap<String, Grad>();

    private void parse(InputStream in) throws Exception {
        XmlPullParser p = Xml.newPullParser();
        p.setInput(in, null);
        Style cur = new Style();
        ArrayList<Style> stack = new ArrayList<Style>();
        Grad grad = null;
        for (int ev = p.getEventType(); ev != XmlPullParser.END_DOCUMENT; ev = p.next()) {
            if (ev == XmlPullParser.START_TAG) {
                String n = p.getName();
                HashMap<String, String> at = attrs(p);
                if (n.equals("svg")) {
                    String vb = at.get("viewBox");
                    if (vb != null) {
                        float[] f = floats(vb);
                        if (f.length == 4) view.set(f[0], f[1], f[0] + f[2], f[1] + f[3]);
                    }
                } else if (n.equals("linearGradient") || n.equals("radialGradient")) {
                    grad = new Grad();
                    grad.linear = n.startsWith("l");
                    grad.at = at;
                    if (at.get("id") != null) grads.put(at.get("id"), grad);
                } else if (n.equals("stop")) {
                    if (grad != null) {
                        int col = color(at.get("stop-color") == null ? "black" : at.get("stop-color"));
                        float so = at.get("stop-opacity") == null ? 1f : num(at.get("stop-opacity"), 1f);
                        col = (Math.round(Color.alpha(col) * so) << 24) | (col & 0xFFFFFF);
                        grad.offs.add(num(at.get("offset") == null ? "0" : at.get("offset"), 0f));
                        grad.cols.add(col);
                    }
                } else if (n.equals("g")) {
                    stack.add(cur);
                    cur = cur.copy();
                    cur.apply(at, true);
                } else {
                    Path path = shapePath(n, at);
                    if (path != null) {
                        Style s = cur.copy();
                        s.apply(at, false);
                        shapes.add(makeShape(path, s));
                    }
                }
            } else if (ev == XmlPullParser.END_TAG) {
                String n = p.getName();
                if (n.equals("g") && !stack.isEmpty()) cur = stack.remove(stack.size() - 1);
                else if (n.endsWith("Gradient")) grad = null;
            }
        }
    }

    private static HashMap<String, String> attrs(XmlPullParser p) {
        HashMap<String, String> at = new HashMap<String, String>();
        for (int i = 0; i < p.getAttributeCount(); i++) at.put(p.getAttributeName(i), p.getAttributeValue(i).trim());
        String style = at.get("style");
        if (style != null) {
            for (String decl : style.split(";")) {
                int k = decl.indexOf(':');
                if (k > 0) at.put(decl.substring(0, k).trim(), decl.substring(k + 1).trim());
            }
        }
        return at;
    }

    private static float num(String s, float def) {
        try {
            s = s.trim();
            if (s.endsWith("%")) return Float.parseFloat(s.substring(0, s.length() - 1)) / 100f;
            if (s.endsWith("px")) s = s.substring(0, s.length() - 2);
            return Float.parseFloat(s);
        } catch (RuntimeException e) {
            return def;
        }
    }

    private static float attr(HashMap<String, String> at, String key, float def) {
        String v = at.get(key);
        return v == null ? def : num(v, def);
    }

    private static float[] floats(String s) {
        String[] parts = s.trim().split("[\\s,]+");
        float[] f = new float[parts.length];
        for (int i = 0; i < parts.length; i++) f[i] = num(parts[i], 0f);
        return f;
    }

    static int color(String s) {
        s = s.trim();
        if (s.startsWith("#") && s.length() == 4) {
            char r = s.charAt(1), g = s.charAt(2), b = s.charAt(3);
            s = "#" + r + r + g + g + b + b;
        }
        if (s.equals("none") || s.equals("transparent")) return 0;
        try {
            return Color.parseColor(s);
        } catch (RuntimeException e) {
            return 0xFF000000;
        }
    }

    /** transform="translate(..) rotate(a cx cy) scale(..) matrix(..)": the list applies right to left, as in SVG. */
    private static Matrix transform(String s) {
        Matrix m = new Matrix();
        int i = 0;
        while (true) {
            int open = s.indexOf('(', i);
            if (open < 0) break;
            int close = s.indexOf(')', open);
            if (close < 0) break;
            String name = s.substring(i, open).replace(",", " ").trim();
            float[] f = floats(s.substring(open + 1, close));
            Matrix t = new Matrix();
            if (name.equals("translate")) t.setTranslate(f[0], f.length > 1 ? f[1] : 0f);
            else if (name.equals("scale")) t.setScale(f[0], f.length > 1 ? f[1] : f[0]);
            else if (name.equals("rotate")) {
                if (f.length >= 3) t.setRotate(f[0], f[1], f[2]); else t.setRotate(f[0]);
            } else if (name.equals("matrix") && f.length == 6) {
                t.setValues(new float[] { f[0], f[2], f[4], f[1], f[3], f[5], 0, 0, 1 });
            }
            m.preConcat(t);
            i = close + 1;
        }
        return m;
    }

    private Path shapePath(String n, HashMap<String, String> at) {
        Path p = new Path();
        if (n.equals("path")) {
            String d = at.get("d");
            if (d == null) return null;
            pathData(p, d);
        } else if (n.equals("circle")) {
            p.addCircle(attr(at, "cx", 0f), attr(at, "cy", 0f), attr(at, "r", 0f), Path.Direction.CW);
        } else if (n.equals("ellipse")) {
            float cx = attr(at, "cx", 0f), cy = attr(at, "cy", 0f), rx = attr(at, "rx", 0f), ry = attr(at, "ry", 0f);
            p.addOval(new RectF(cx - rx, cy - ry, cx + rx, cy + ry), Path.Direction.CW);
        } else if (n.equals("rect")) {
            float x = attr(at, "x", 0f), y = attr(at, "y", 0f), w = attr(at, "width", 0f), h = attr(at, "height", 0f);
            float rx = attr(at, "rx", -1f), ry = attr(at, "ry", -1f);
            if (rx < 0) rx = ry < 0 ? 0f : ry;
            if (ry < 0) ry = rx;
            p.addRoundRect(new RectF(x, y, x + w, y + h), Math.min(rx, w / 2f), Math.min(ry, h / 2f), Path.Direction.CW);
        } else if (n.equals("line")) {
            p.moveTo(attr(at, "x1", 0f), attr(at, "y1", 0f));
            p.lineTo(attr(at, "x2", 0f), attr(at, "y2", 0f));
        } else if (n.equals("polygon") || n.equals("polyline")) {
            float[] f = floats(at.get("points") == null ? "" : at.get("points"));
            for (int i = 0; i + 1 < f.length; i += 2) {
                if (i == 0) p.moveTo(f[0], f[1]); else p.lineTo(f[i], f[i + 1]);
            }
            if (n.equals("polygon")) p.close();
        } else {
            return null;
        }
        return p;
    }

    private Shape makeShape(Path path, Style s) {
        path.transform(s.m);
        RectF b = new RectF();
        path.computeBounds(b, true);
        Shape sh = new Shape();
        sh.path = path;
        sh.layer = s.layer;
        if (!s.fill.equals("none")) {
            sh.fill = new Paint(Paint.ANTI_ALIAS_FLAG);
            sh.fill.setStyle(Paint.Style.FILL);
            paintFor(sh, true, s.fill, s.opacity * s.fillOpacity, b, s.m);
        }
        if (!s.stroke.equals("none")) {
            sh.stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
            sh.stroke.setStyle(Paint.Style.STROKE);
            sh.fixed = s.fixed;
            sh.strokeW = s.strokeWidth;
            sh.stroke.setStrokeWidth(s.fixed ? s.strokeWidth : s.strokeWidth * s.m.mapRadius(1f));
            sh.stroke.setStrokeCap(s.cap.equals("round") ? Paint.Cap.ROUND : s.cap.equals("square") ? Paint.Cap.SQUARE : Paint.Cap.BUTT);
            sh.stroke.setStrokeJoin(s.join.equals("round") ? Paint.Join.ROUND : s.join.equals("bevel") ? Paint.Join.BEVEL : Paint.Join.MITER);
            paintFor(sh, false, s.stroke, s.opacity * s.strokeOpacity, b, s.m);
        }
        return sh;
    }

    private void paintFor(Shape sh, boolean fill, String spec, float opacity, RectF bounds, Matrix m) {
        Paint paint = fill ? sh.fill : sh.stroke;
        int a = Math.round(255 * Math.max(0f, Math.min(1f, opacity)));
        if (spec.startsWith("url(")) {
            int end = spec.indexOf(')');
            String id = spec.substring(spec.indexOf('#') + 1, end < 0 ? spec.length() : end).trim();
            Grad g = grads.get(id);
            Shader shader = g == null ? null : shader(g, bounds, m);
            if (shader != null) {
                paint.setShader(shader);
            } else {
                paint.setColor(0xFF888888);
            }
        } else if (spec.equals("currentColor")) {
            if (fill) sh.fillTint = true; else sh.strokeTint = true;
        } else {
            int col = color(spec);
            paint.setColor(col | 0xFF000000);
            a = a * Color.alpha(col) / 255;
        }
        if (fill) sh.fillA = a; else sh.strokeA = a;
    }

    private static Shader shader(Grad g, RectF b, Matrix m) {
        int n = g.cols.size();
        if (n == 0) return null;
        int[] cols = new int[Math.max(2, n)];
        float[] pos = new float[cols.length];
        for (int i = 0; i < cols.length; i++) {
            int k = Math.min(i, n - 1);
            cols[i] = g.cols.get(k);
            pos[i] = g.offs.get(k);
            if (i > 0 && pos[i] < pos[i - 1]) pos[i] = pos[i - 1];
        }
        Shader sh;
        if (g.linear) {
            sh = new LinearGradient(attr(g.at, "x1", 0f), attr(g.at, "y1", 0f), attr(g.at, "x2", 1f), attr(g.at, "y2", 0f), cols, pos, Shader.TileMode.CLAMP);
        } else {
            float r = attr(g.at, "r", 0.5f);
            sh = new RadialGradient(attr(g.at, "cx", 0.5f), attr(g.at, "cy", 0.5f), r <= 0 ? 0.0001f : r, cols, pos, Shader.TileMode.CLAMP);
        }
        Matrix lm = new Matrix();
        if ("userSpaceOnUse".equals(g.at.get("gradientUnits"))) {
            lm.set(m);
        } else {
            lm.setScale(Math.max(b.width(), 0.0001f), Math.max(b.height(), 0.0001f));
            lm.postTranslate(b.left, b.top);
        }
        sh.setLocalMatrix(lm);
        return sh;
    }

    // ---- path data -----------------------------------------------------------------------------------------------------

    private static final class Reader {
        final String s;
        int i;

        Reader(String s) {
            this.s = s;
        }

        void skip() {
            while (i < s.length() && (s.charAt(i) == ' ' || s.charAt(i) == ',' || s.charAt(i) == '\n' || s.charAt(i) == '\t' || s.charAt(i) == '\r')) i++;
        }

        boolean more() {
            skip();
            return i < s.length();
        }

        boolean atNumber() {
            skip();
            if (i >= s.length()) return false;
            char c = s.charAt(i);
            return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
        }

        float number() {
            skip();
            int st = i;
            if (i < s.length() && (s.charAt(i) == '-' || s.charAt(i) == '+')) i++;
            boolean dot = false;
            while (i < s.length()) {
                char c = s.charAt(i);
                if (c >= '0' && c <= '9') {
                    i++;
                } else if (c == '.' && !dot) {
                    dot = true;
                    i++;
                } else if ((c == 'e' || c == 'E') && i + 1 < s.length() && (Character.isDigit(s.charAt(i + 1)) || s.charAt(i + 1) == '-')) {
                    i += 2;
                } else {
                    break;
                }
            }
            return i > st ? Float.parseFloat(s.substring(st, i)) : 0f;
        }

        boolean flag() {
            skip();
            boolean v = i < s.length() && s.charAt(i) == '1';
            i++;
            return v;
        }
    }

    private static void pathData(Path p, String d) {
        Reader r = new Reader(d);
        char cmd = 0, prev = 0;
        float x = 0, y = 0, sx = 0, sy = 0, cx2 = 0, cy2 = 0;
        while (r.more()) {
            char c = r.s.charAt(r.i);
            if (Character.isLetter(c)) {
                cmd = c;
                r.i++;
            } else if (cmd == 0) {
                break;
            } else if (cmd == 'M') {
                cmd = 'L';
            } else if (cmd == 'm') {
                cmd = 'l';
            }
            boolean rel = Character.isLowerCase(cmd);
            float ox = rel ? x : 0f, oy = rel ? y : 0f;
            switch (Character.toUpperCase(cmd)) {
                case 'M':
                    x = ox + r.number();
                    y = oy + r.number();
                    p.moveTo(x, y);
                    sx = x;
                    sy = y;
                    break;
                case 'L':
                    x = ox + r.number();
                    y = oy + r.number();
                    p.lineTo(x, y);
                    break;
                case 'H':
                    x = ox + r.number();
                    p.lineTo(x, y);
                    break;
                case 'V':
                    y = oy + r.number();
                    p.lineTo(x, y);
                    break;
                case 'C': {
                    float x1 = ox + r.number(), y1 = oy + r.number(), x2 = ox + r.number(), y2 = oy + r.number(), ex = ox + r.number(), ey = oy + r.number();
                    p.cubicTo(x1, y1, x2, y2, ex, ey);
                    cx2 = x2;
                    cy2 = y2;
                    x = ex;
                    y = ey;
                    break;
                }
                case 'S': {
                    boolean after = Character.toUpperCase(prev) == 'C' || Character.toUpperCase(prev) == 'S';
                    float x1 = after ? 2 * x - cx2 : x, y1 = after ? 2 * y - cy2 : y;
                    float x2 = ox + r.number(), y2 = oy + r.number(), ex = ox + r.number(), ey = oy + r.number();
                    p.cubicTo(x1, y1, x2, y2, ex, ey);
                    cx2 = x2;
                    cy2 = y2;
                    x = ex;
                    y = ey;
                    break;
                }
                case 'Q': {
                    float x1 = ox + r.number(), y1 = oy + r.number(), ex = ox + r.number(), ey = oy + r.number();
                    p.quadTo(x1, y1, ex, ey);
                    x = ex;
                    y = ey;
                    break;
                }
                case 'A': {
                    float rx = r.number(), ry = r.number();
                    r.number(); // x-axis rotation: not supported
                    boolean large = r.flag(), sweep = r.flag();
                    float ex = ox + r.number(), ey = oy + r.number();
                    arc(p, x, y, rx, ry, large, sweep, ex, ey);
                    x = ex;
                    y = ey;
                    break;
                }
                case 'Z':
                    p.close();
                    x = sx;
                    y = sy;
                    break;
                default:
                    return;
            }
            prev = cmd;
        }
    }

    /** An SVG elliptical arc (no x-axis rotation) from (x0,y0) to (x,y) as Path.arcTo. */
    private static void arc(Path p, float x0, float y0, float rx, float ry, boolean large, boolean sweep, float x, float y) {
        rx = Math.abs(rx);
        ry = Math.abs(ry);
        if (rx == 0 || ry == 0 || (x0 == x && y0 == y)) {
            p.lineTo(x, y);
            return;
        }
        double dx = (x0 - x) / 2.0, dy = (y0 - y) / 2.0;
        double lambda = dx * dx / ((double) rx * rx) + dy * dy / ((double) ry * ry);
        if (lambda > 1) {
            double sq = Math.sqrt(lambda);
            rx *= sq;
            ry *= sq;
        }
        double rx2 = (double) rx * rx, ry2 = (double) ry * ry;
        double num = rx2 * ry2 - rx2 * dy * dy - ry2 * dx * dx, den = rx2 * dy * dy + ry2 * dx * dx;
        double coef = Math.sqrt(Math.max(0, num / den)) * (large == sweep ? -1 : 1);
        double cxp = coef * rx * dy / ry, cyp = -coef * ry * dx / rx;
        double cx = cxp + (x0 + x) / 2.0, cy = cyp + (y0 + y) / 2.0;
        double t1 = Math.atan2((dy - cyp) / ry, (dx - cxp) / rx);
        double t2 = Math.atan2((-dy - cyp) / ry, (-dx - cxp) / rx);
        double dt = t2 - t1;
        if (!sweep && dt > 0) dt -= 2 * Math.PI;
        if (sweep && dt < 0) dt += 2 * Math.PI;
        p.arcTo(new RectF((float) (cx - rx), (float) (cy - ry), (float) (cx + rx), (float) (cy + ry)), (float) Math.toDegrees(t1), (float) Math.toDegrees(dt));
    }
}
