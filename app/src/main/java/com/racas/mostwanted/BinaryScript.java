package com.racas.mostwanted;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class BinaryScript {
    interface Chooser {
        int choose(String title, List<String> options);
    }

    static final Chooser FIRST = (title, options) -> {
        for (int i = 0; i < options.size(); i++) {
            String o = options.get(i).toLowerCase(Locale.ROOT);
            if (o.contains("install") && !o.contains("uninstall")) return i;
        }
        return 0;
    };

    static final class Result {
        final Map<String, Map<Integer, String>> strings = new LinkedHashMap<>();
        final List<String> textureFolders = new ArrayList<>();
        final Map<String, String> files = new LinkedHashMap<>();
        final Map<String, String> imports = new LinkedHashMap<>();
        final Map<String, Map<String, String>> textureUpdates = new LinkedHashMap<>();
        boolean ran;
    }

    private static final int MAX_DEPTH = 32, MAX_LINES = 200000;
    private static final Pattern LANGUAGE = Pattern.compile("(?:.*\\\\)?([A-Z_]+)\\.BIN");
    private static final Pattern SETUP_SCRIPT = Pattern.compile("\"Endscript\"\\s*:\\s*\"([^\"]*)\"", Pattern.CASE_INSENSITIVE);

    private final Map<String, byte[]> mod;
    private final Map<String, String> lower = new HashMap<>();
    private final Predicate<String> gameFile;
    private final Chooser chooser;
    private final Consumer<String> log;
    private final Result result = new Result();
    private final Set<String> made = new HashSet<>();
    private int lines;

    private BinaryScript(Map<String, byte[]> mod, Predicate<String> gameFile, Chooser chooser, Consumer<String> log) {
        this.mod = mod;
        this.gameFile = gameFile;
        this.chooser = chooser;
        this.log = log;
        for (String k : mod.keySet()) lower.put(k.toLowerCase(Locale.ROOT), k);
    }

    static Result run(Map<String, byte[]> mod, Predicate<String> gameFile, Chooser chooser, Consumer<String> log) {
        BinaryScript s = new BinaryScript(mod, gameFile, chooser != null ? chooser : FIRST, log);
        String entry = s.entry();
        if (entry != null) {
            s.result.ran = true;
            s.runFile(entry, 0);
        }
        return s.result;
    }

    private String entry() {
        List<String> ends = new ArrayList<>();
        for (String k : mod.keySet()) if (k.toLowerCase(Locale.ROOT).endsWith(".end")) ends.add(k);
        ends.sort((a, b) -> a.split("/").length != b.split("/").length ? a.split("/").length - b.split("/").length : a.compareTo(b));
        for (String k : ends) if (text(mod.get(k)).trim().startsWith("[VERSN1]")) return k;
        for (String k : ends) {
            String n = k.substring(k.lastIndexOf('/') + 1).toLowerCase(Locale.ROOT);
            if (n.contains("install") && !n.contains("uninstall")) return k;
        }
        return null;
    }

    private static String text(byte[] b) {
        String s = new String(b, StandardCharsets.UTF_8);
        return s.startsWith("\uFEFF") ? s.substring(1) : s;
    }

    private static String dir(String path) {
        int i = path.lastIndexOf('/');
        return i < 0 ? "" : path.substring(0, i);
    }

    private String resolve(String base, String rel) {
        String p = (base.isEmpty() ? "" : base + "/") + rel.replace('\\', '/');
        List<String> parts = new ArrayList<>();
        for (String s : p.split("/")) {
            if (s.isEmpty() || s.equals(".")) continue;
            if (s.equals("..")) {
                if (!parts.isEmpty()) parts.remove(parts.size() - 1);
            } else {
                parts.add(s);
            }
        }
        String joined = String.join("/", parts);
        String found = lower.get(joined.toLowerCase(Locale.ROOT));
        return found != null ? found : joined;
    }

    private static String gamePath(String p) {
        return p.replace('/', '\\').toUpperCase(Locale.ROOT);
    }

    private String root;

    private void runFile(String path, int depth) {
        if (depth > MAX_DEPTH) return;
        byte[] b = mod.get(path);
        if (b == null) {
            log.accept("script: no " + path);
            return;
        }
        String body = text(b);
        String head = body.trim();
        if (head.startsWith("[VERSN1]")) {
            runSetup(path, head.substring("[VERSN1]".length()), depth);
            return;
        }
        if (root == null) root = dir(path);
        List<List<String>> program = new ArrayList<>();
        for (String raw : body.split("\r?\n")) {
            List<String> t = tokens(raw);
            if (!t.isEmpty()) program.add(t);
        }
        exec(program, 0, program.size(), root, depth);
    }

    private void runSetup(String path, String json, int depth) {
        Matcher m = SETUP_SCRIPT.matcher(json);
        if (m.find() && !m.group(1).isEmpty()) runFile(resolve(dir(path), m.group(1)), depth + 1);
    }

    static List<String> tokens(String line) {
        List<String> out = new ArrayList<>();
        int i = 0, n = line.length();
        while (i < n) {
            char c = line.charAt(i);
            if (Character.isWhitespace(c)) {
                i++;
                continue;
            }
            if (c == '/' && i + 1 < n && line.charAt(i + 1) == '/') break;
            if (c == '"') {
                int end = line.indexOf('"', i + 1);
                if (end < 0) end = n;
                out.add("\"" + line.substring(i + 1, end));
                i = end + 1;
                continue;
            }
            int start = i;
            while (i < n && !Character.isWhitespace(line.charAt(i))) i++;
            out.add(line.substring(start, i));
        }
        return out;
    }

    private static boolean quoted(String t) {
        return t.startsWith("\"");
    }

    private static String value(String t) {
        return quoted(t) ? t.substring(1) : t;
    }

    private static String word(List<String> t, int i) {
        return i < t.size() ? t.get(i).toLowerCase(Locale.ROOT) : "";
    }

    private int blockEnd(List<List<String>> p, int from, int to) {
        int depth = 0;
        for (int i = from; i < to; i++) {
            String w = word(p.get(i), 0);
            if (w.equals("if") || w.equals("combobox") || w.equals("checkbox")) depth++;
            else if (w.equals("end")) {
                if (depth == 0) return i;
                depth--;
            }
        }
        return to;
    }

    private void exec(List<List<String>> p, int from, int to, String base, int depth) {
        for (int i = from; i < to; i++) {
            if (++lines > MAX_LINES) return;
            List<String> t = p.get(i);
            String cmd = word(t, 0);
            switch (cmd) {
                case "if": {
                    int end = blockEnd(p, i + 1, to);
                    int start = i + 1;
                    if (start < end && word(p.get(start), 0).equals("do")) start++;
                    int split = end;
                    int nest = 0;
                    for (int k = start; k < end; k++) {
                        String w = word(p.get(k), 0);
                        if (w.equals("if") || w.equals("combobox") || w.equals("checkbox")) nest++;
                        else if (w.equals("end")) nest--;
                        else if (w.equals("else") && nest == 0) {
                            split = k;
                            break;
                        }
                    }
                    if (condition(t, base)) exec(p, start, split, base, depth);
                    else if (split < end) exec(p, split + 1, end, base, depth);
                    i = end;
                    break;
                }
                case "combobox":
                case "checkbox": {
                    int end = blockEnd(p, i + 1, to);
                    List<String> options = new ArrayList<>();
                    for (int k = 1; k < t.size(); k++) if (quoted(t.get(k))) options.add(value(t.get(k)));
                    String title = options.size() > 1 ? options.remove(options.size() - 1) : "";
                    Map<String, int[]> blocks = new LinkedHashMap<>();
                    String current = null;
                    int currentStart = i + 1;
                    for (int k = i + 1; k <= end; k++) {
                        List<String> line = k < end ? p.get(k) : null;
                        boolean label = line != null && line.size() == 1 && quoted(line.get(0)) && options.contains(value(line.get(0)));
                        if (label || k == end) {
                            if (current != null) blocks.put(current, new int[] {currentStart, k});
                            if (label) {
                                current = value(line.get(0));
                                currentStart = k + 1;
                            }
                        }
                    }
                    if (!options.isEmpty()) {
                        int pick = cmd.equals("checkbox") ? -1 : chooser.choose(title, options);
                        for (int k = 0; k < options.size(); k++) {
                            boolean on = cmd.equals("checkbox") ? FIRST.choose(title, List.of(options.get(k))) == 0 : k == pick;
                            int[] r = blocks.get(options.get(k));
                            if (on && r != null) exec(p, r[0], r[1], base, depth);
                        }
                    }
                    i = end;
                    break;
                }
                case "append":
                    if (t.size() > 1) runFile(resolve(base, value(t.get(1))), depth + 1);
                    break;
                case "add_string":
                    addString(t);
                    break;
                case "bind_textures":
                case "replace_textures":
                    if (t.size() > 1) {
                        int at = word(t, 1).equals("override") ? 2 : 1;
                        if (at + 3 < t.size()) result.textureFolders.add(resolve(base, value(t.get(at + 3))));
                    }
                    break;
                case "update_texture":
                    if (t.size() > 6)
                        result.textureUpdates.computeIfAbsent(value(t.get(4)).toUpperCase(Locale.ROOT), x -> new LinkedHashMap<>())
                                .put(value(t.get(5)), value(t.get(6)));
                    break;
                case "move_file":
                case "copy_file":
                    fileCommand(t, base);
                    break;
                case "create_folder":
                    if (t.size() > 2) made.add(gamePath(value(t.get(t.size() - 1))));
                    break;
                case "import":
                    if (t.size() > 3) {
                        int at = word(t, 1).equals("override") || word(t, 1).equals("synchronize") || word(t, 1).equals("negate") ? 2 : 1;
                        if (at + 2 < t.size()) result.imports.put(value(t.get(at + 1)).toLowerCase(Locale.ROOT), resolve(base, value(t.get(at + 2))));
                    }
                    break;
                default:
                    break;
            }
        }
    }

    private boolean condition(List<String> t, String base) {
        String kind = word(t, 1);
        String path = t.size() > 3 ? value(t.get(3)) : t.size() > 2 ? value(t.get(2)) : "";
        boolean absolute = word(t, 2).equals("absolute");
        switch (kind) {
            case "file_exists":
                if (absolute) return gameFile.test(gamePath(path)) || result.files.containsKey(gamePath(path));
                return mod.containsKey(resolve(base, path));
            case "directory_exists":
                if (absolute) return made.contains(gamePath(path));
                String d = resolve(base, path).toLowerCase(Locale.ROOT) + "/";
                for (String k : lower.keySet()) if (k.startsWith(d)) return true;
                return false;
            case "collection_exists":
                return true;
            default:
                return false;
        }
    }

    private void fileCommand(List<String> t, String base) {
        List<String> args = new ArrayList<>();
        boolean srcRelative = true;
        int seen = 0;
        for (int k = 1; k < t.size(); k++) {
            String w = t.get(k).toLowerCase(Locale.ROOT);
            if (w.equals("override")) continue;
            if (w.equals("relative") || w.equals("absolute")) {
                if (seen++ == 0) srcRelative = w.equals("relative");
                continue;
            }
            args.add(value(t.get(k)));
        }
        if (args.size() < 2) return;
        String src = srcRelative ? resolve(base, args.get(0)) : args.get(0);
        result.files.put(gamePath(args.get(1)), src);
    }

    private void addString(List<String> t) {
        if (t.size() < 6) return;
        Matcher m = LANGUAGE.matcher(gamePath(value(t.get(1))));
        if (!m.matches()) return;
        String language = m.group(1).toLowerCase(Locale.ROOT);
        String key = value(t.get(4)), label = value(t.get(5));
        String text = t.size() > 6 ? value(t.get(6)) : label;
        int hash;
        if (key.regionMatches(true, 0, "0x", 0, 2)) {
            try {
                hash = (int) Long.parseLong(key.substring(2), 16);
            } catch (NumberFormatException e) {
                hash = CarConverter.bhash(label);
            }
        } else {
            hash = CarConverter.bhash(t.size() > 6 ? label : key);
            if (t.size() <= 6) text = label;
        }
        result.strings.computeIfAbsent(language, x -> new LinkedHashMap<>()).put(hash, text);
    }
}
