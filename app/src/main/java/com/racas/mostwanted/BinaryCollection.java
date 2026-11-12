package com.racas.mostwanted;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

final class BinaryCollection {
    static final int DB_MODEL_PARTS = CarConverter.bhash("DBModelParts");
    static final int CAR_TYPE_INFOS = CarConverter.bhash("CarTypeInfos");
    static final int COLLISIONS = CarConverter.bhash("Collisions");

    static final int BOOL = 0, INT = 1, FLOAT = 2, KEY = 3, STRING = 4, TWO_STRING = 5, COLOR = 6, CUSTOM = 7;

    private static final Map<Integer, Integer> KEYS = new HashMap<>();

    static {
        for (String n : new String[] {"STOCK", "REMAP", "ONLINE", "CENTER", "MIRROR", "ISDECAL", "FULLBODY", "HOODLEFT", "HOODRIGHT",
                "VERTSPLIT", "WHEELLEFT", "WHEELRIGHT", "USEMARKER1", "USEMARKER2", "COMPLEXTARGET", "CARBONFIBRE", "EXCLUDE_SUV",
                "EXCLUDE_UG1", "EXCLUDE_UG2", "WARNONDELETE", "KITNOTPAINT01", "KITNOTPAINT03", "KITNOTPAINT04", "KITNOTPAINT06",
                "KITNOTPAINT07", "KITNOTPAINT10", "KITNOTPAINT18", "STOCK_MATERIAL"})
            KEYS.put(CarConverter.bhash(n), BOOL);
        KEYS.put(0x6509EC92, BOOL);
        for (String n : new String[] {"BLEND", "HOODHUE", "HOODLUM", "HOODSAT", "TIREHUE", "TIRELUM", "TIRESAT", "SPINNEROFFSET"})
            KEYS.put(CarConverter.bhash(n), FLOAT);
        KEYS.put(0x9A9B6DDC, FLOAT);
        for (String n : new String[] {"COLOR0ID", "COLOR1ID", "COLOR2ID", "COLOR3ID"}) KEYS.put(CarConverter.bhash(n), INT);
        for (String n : new String[] {"TEXTURE", "NAME_OFFSET", "LOD_CHARACTERS_OFFSET"}) KEYS.put(CarConverter.bhash(n), STRING);
        for (String n : new String[] {"LOD_BASE_NAME", "PART_NAME_OFFSETS", "PARTID_UPGRADE_GROUP", "MODEL_TABLE_OFFSET"})
            KEYS.put(CarConverter.bhash(n), INT);
        for (String n : new String[] {"CV", "MAT0", "MAT1", "MAT2", "MAT3", "MAT4", "MAT5", "MAT6", "MAT7", "NAME", "SIZE", "SHAPE",
                "SWATCH", "MATNAMEA", "MATNAMEB", "HOODUNDER", "BRAND_NAME", "PAINTGROUP", "HOODEMITTER", "TEXTUREHASH",
                "EXCLUDEDECAL", "LANGUAGEHASH", "SPEECHCOLOUR", "TEXTURE_NAME", "WHEELEMITTER", "SPECIFICCARNAME",
                "SPINNER_TEXTURE", "GROUPLANGUAGEHASH", "VINYLLANGUAGEHASH", "COLOR0LANGUAGEHASH", "COLOR1LANGUAGEHASH",
                "COLOR2LANGUAGEHASH", "COLOR3LANGUAGEHASH", "LIGHT_MATERIAL_NAME", "PART_NAME_BASE_HASH",
                "LOD_NAME_PREFIX_NAMEHASH"})
            KEYS.put(CarConverter.bhash(n), KEY);
        for (String n : new String[] {"RED", "RED2", "BLUE", "ALPHA", "GREEN", "GLOSS", "BLUE2", "ALPHA2", "GREEN2", "MAX_LOD",
                "DISPRED", "DISPBLUE", "HUDINDEX", "ANIMSTYLE", "DISPGREEN", "KITNUMBER", "NUMCOLOURS", "NUM_DECALS", "DAMAGELEVEL",
                "SPOKE_COUNT", "INNER_RADIUS", "OUTER_RADIUS", "LANGUAGEHASHABR", "NUMREMAPCOLOURS", "MORPHTARGET_NUM",
                "PART_NAME_SELECTOR", "LOD_NAME_PREFIX_SELECTOR"})
            KEYS.put(CarConverter.bhash(n), INT);
        KEYS.put(0x7D29CF3E, INT);
    }

    static final class Attribute {
        int key, kind;
        int value;
        String text, text2;
    }

    static final class Lod {
        boolean templated;
        String concatenator;
        final String[] names = new String[5];
    }

    static final class Part {
        String label, debugName;
        int slot, upgrade;
        Lod lod;
        final List<Attribute> attributes = new ArrayList<>();
    }

    static final class Model {
        String name;
        final List<Part> parts = new ArrayList<>();
    }

    static final class Malformed extends Exception {
        Malformed(String what) {
            super(what);
        }
    }

    private BinaryCollection() {}

    private static final class Reader {
        final byte[] b;
        int at;

        Reader(byte[] b) {
            this.b = b;
        }

        void need(int n) throws Malformed {
            if (at + n > b.length) throw new Malformed("the collection ends early");
        }

        int u8() throws Malformed {
            need(1);
            return b[at++] & 255;
        }

        int u16() throws Malformed {
            need(2);
            int v = (b[at] & 255) | (b[at + 1] & 255) << 8;
            at += 2;
            return v;
        }

        int i32() throws Malformed {
            need(4);
            int v = (b[at] & 255) | (b[at + 1] & 255) << 8 | (b[at + 2] & 255) << 16 | (b[at + 3] & 255) << 24;
            at += 4;
            return v;
        }

        String text() throws Malformed {
            int end = at;
            while (end < b.length && b[end] != 0) end++;
            if (end >= b.length) throw new Malformed("a name runs past the collection");
            String s = new String(b, at, end - at, StandardCharsets.UTF_8);
            at = end + 1;
            return s;
        }

        byte[] bytes(int n) throws Malformed {
            need(n);
            byte[] out = Arrays.copyOfRange(b, at, at + n);
            at += n;
            return out;
        }
    }

    static boolean isCollection(byte[] d) {
        return d != null && d.length >= 20 && d[0] == 'g' && d[1] == 'M' && d[2] == 'p' && d[3] == 'B';
    }

    static int manager(byte[] d) {
        return isCollection(d) ? CarConverter.i32(d, 12) : 0;
    }

    static byte[] body(byte[] d) throws Malformed {
        if (!isCollection(d)) throw new Malformed("not a Binary collection");
        int n = CarConverter.i32(d, 16);
        if (n < 0 || 20L + n > d.length) throw new Malformed("a collection's size");
        try {
            return CarConverter.unpackBlock(Arrays.copyOfRange(d, 20, 20 + n));
        } catch (CarConverter.Failure e) {
            throw new Malformed(e.getMessage());
        }
    }

    static String name(byte[] d) {
        try {
            return new Reader(body(d)).text().toUpperCase(Locale.ROOT);
        } catch (Malformed e) {
            return null;
        }
    }

    static Model model(byte[] d) throws Malformed {
        Reader r = new Reader(body(d));
        Model m = new Model();
        m.name = r.text().toUpperCase(Locale.ROOT);
        int count = r.i32();
        if (count < 0 || count > 20000) throw new Malformed("a part count of " + count);
        for (int i = 0; i < count; i++) {
            int attributes = r.i32();
            if (attributes < 0 || attributes > 4096) throw new Malformed("an attribute count of " + attributes);
            Part p = new Part();
            p.label = r.text();
            p.debugName = r.text();
            p.slot = r.u8();
            p.upgrade = r.u16();
            if (r.u8() != 0) {
                Lod l = new Lod();
                l.templated = r.u8() != 0;
                if (r.u8() != 0) l.concatenator = r.text();
                boolean[] has = new boolean[5];
                for (int k = 0; k < 5; k++) has[k] = r.u8() != 0;
                for (int k = 0; k < 5; k++) if (has[k]) l.names[k] = r.text();
                p.lod = l;
            }
            for (int k = 0; k < attributes; k++) p.attributes.add(attribute(r));
            m.parts.add(p);
        }
        return m;
    }

    private static Attribute attribute(Reader r) throws Malformed {
        Attribute a = new Attribute();
        a.key = r.i32();
        Integer known = KEYS.get(a.key);
        int kind = known != null ? known : CUSTOM;
        if (kind == CUSTOM) {
            String name = r.text();
            int type = r.i32();
            kind = customKind(type);
            if (kind == CUSTOM) throw new Malformed("the attribute " + name + " is of a type " + type + " no reader knows");
        }
        a.kind = kind;
        switch (kind) {
            case BOOL:
                a.value = r.u8() != 0 ? 1 : 0;
                break;
            case INT:
            case FLOAT:
                a.value = r.i32();
                break;
            case KEY: {
                String s = r.text();
                a.value = s.isEmpty() ? -1 : hashOrHex(s);
                a.text = s;
                break;
            }
            case STRING:
                if (r.u8() != 0) a.text = r.text();
                break;
            case TWO_STRING: {
                boolean one = r.u8() != 0, two = r.u8() != 0;
                if (one) a.text = r.text();
                if (two) a.text2 = r.text();
                break;
            }
            case COLOR: {
                int alpha = r.u8(), blue = r.u8(), green = r.u8(), red = r.u8();
                a.value = red | green << 8 | blue << 16 | alpha << 24;
                break;
            }
            default:
                a.value = 0;
        }
        return a;
    }

    private static int customKind(int type) {
        switch (type) {
            case 1: return BOOL;
            case 2: return INT;
            case 3: return FLOAT;
            case 4: return STRING;
            case 5: return TWO_STRING;
            case 6: return COLOR;
            case 8: return KEY;
            default: return CUSTOM;
        }
    }

    private static int hashOrHex(String s) {
        if (s.length() == 10 && s.regionMatches(true, 0, "0x", 0, 2)) {
            try {
                return (int) Long.parseLong(s.substring(2), 16);
            } catch (NumberFormatException e) {
                return CarConverter.bhash(s);
            }
        }
        return CarConverter.bhash(s);
    }

    static byte[] carType(byte[] d, String car, int type) throws Malformed {
        Reader r = new Reader(body(d));
        byte[] out = new byte[0xD0];
        String name = r.text().toUpperCase(Locale.ROOT);
        String make = r.text().toUpperCase(Locale.ROOT);
        if (!name.isEmpty() && !name.equals(car)) throw new Malformed("the car type is " + name + ", not " + car);
        put(out, 0, car, 16);
        put(out, 0x10, car, 16);
        put(out, 0x20, "CARS\\" + car + "\\GEOMETRY.BIN", 32);
        put(out, 0x40, make, 16);
        CarConverter.put32(out, 0x50, CarConverter.bhash(car));
        System.arraycopy(r.bytes(4), 0, out, 0x54, 4);
        System.arraycopy(r.bytes(7), 0, out, 0x58, 7);
        System.arraycopy(r.bytes(48), 0, out, 0x60, 48);
        CarConverter.put32(out, 0x90, type);
        System.arraycopy(r.bytes(8), 0, out, 0x94, 8);
        System.arraycopy(r.bytes(10), 0, out, 0x9C, 10);
        System.arraycopy(r.bytes(20), 0, out, 0xA8, 20);
        System.arraycopy(r.bytes(12), 0, out, 0xBC, 12);
        System.arraycopy(r.bytes(4), 0, out, 0xCC, 4);
        return out;
    }

    static String make(byte[] d) {
        try {
            Reader r = new Reader(body(d));
            r.text();
            return r.text().toUpperCase(Locale.ROOT);
        } catch (Malformed e) {
            return null;
        }
    }

    static byte[] collision(byte[] d, int key) throws Malformed {
        Reader r = new Reader(body(d));
        r.text();
        int bounds = r.i32(), clouds = r.i32();
        if (bounds < 0 || bounds > 4096 || clouds < 0 || clouds > 4096) throw new Malformed("a collision's counts");
        byte[] boundBytes = r.bytes(bounds * 0x30);
        List<byte[]> cloudBytes = new ArrayList<>();
        for (int i = 0; i < clouds; i++) {
            int vertices = r.i32();
            if (vertices < 0 || vertices > 65536) throw new Malformed("a collision cloud's vertices");
            cloudBytes.add(r.bytes(vertices * 16));
        }
        int resolved = r.u8();
        int size = 16 + boundBytes.length + 16;
        for (byte[] c : cloudBytes) size += 16 + c.length;
        byte[] out = new byte[size];
        int at = 0;
        CarConverter.put32(out, at, key);
        CarConverter.put32(out, at + 4, bounds);
        CarConverter.put32(out, at + 8, resolved != 0 ? 1 : 0);
        at += 16;
        System.arraycopy(boundBytes, 0, out, at, boundBytes.length);
        at += boundBytes.length;
        CarConverter.put32(out, at, clouds);
        at += 16;
        for (byte[] c : cloudBytes) {
            CarConverter.put32(out, at, c.length / 16);
            at += 16;
            System.arraycopy(c, 0, out, at, c.length);
            at += c.length;
        }
        return out;
    }

    private static void put(byte[] out, int at, String s, int room) {
        byte[] b = s.getBytes(StandardCharsets.ISO_8859_1);
        System.arraycopy(b, 0, out, at, Math.min(b.length, room - 1));
    }
}
