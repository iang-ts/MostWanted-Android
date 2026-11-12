package com.racas.mostwanted;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.Consumer;

final class UnlimiterImport {
    static final String DIR = "unlimiter", SETTINGS = "NFSMWUnlimiterSettings.ini", GENERAL = "_General.ini", MARK = ".imported";
    static final String VAULT = "vault.nfsms", VAULT_INSTALL = "vlt/install.nfsms";

    private UnlimiterImport() {}

    static File dir() {
        return new File(ModInstaller.modsDir(), DIR);
    }

    static boolean installed() {
        return new File(dir(), MARK).isFile();
    }

    private static String settingsPath(Iterable<String> paths) {
        for (String p : paths) {
            String l = p.toLowerCase(Locale.ROOT);
            if (l.equals(SETTINGS.toLowerCase(Locale.ROOT)) || l.endsWith("/" + SETTINGS.toLowerCase(Locale.ROOT))) return p;
        }
        return null;
    }

    private static String dataRoot(Iterable<String> paths) {
        for (String p : paths) {
            String l = p.toLowerCase(Locale.ROOT);
            int at = l.indexOf("unlimiterdata/" + GENERAL.toLowerCase(Locale.ROOT));
            if (at >= 0 && (at == 0 || l.charAt(at - 1) == '/')) return p.substring(0, at + "unlimiterdata/".length());
        }
        return null;
    }

    static boolean isUnlimiter(Iterable<String> paths) {
        return settingsPath(paths) != null && dataRoot(paths) != null;
    }

    static int install(Map<String, File> files, Consumer<String> log) throws IOException {
        String settings = settingsPath(files.keySet()), root = dataRoot(files.keySet());
        if (settings == null || root == null) return 0;
        File dir = dir(), work = new File(ModInstaller.modsDir(), ".new_" + DIR);
        deleteTree(work);
        if (!work.mkdirs()) throw new IOException("cannot write to " + work);
        Files.copy(files.get(settings).toPath(), new File(work, SETTINGS).toPath(), StandardCopyOption.REPLACE_EXISTING);
        int count = 1;
        String rootLower = root.toLowerCase(Locale.ROOT);
        for (Map.Entry<String, File> e : files.entrySet()) {
            String l = e.getKey().toLowerCase(Locale.ROOT);
            if (!l.startsWith(rootLower) || !l.endsWith(".ini")) continue;
            String rel = e.getKey().substring(root.length());
            if (rel.contains("..")) continue;
            File out = new File(work, rel);
            File parent = out.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("cannot write to " + parent);
            Files.copy(e.getValue().toPath(), out.toPath(), StandardCopyOption.REPLACE_EXISTING);
            count++;
        }
        writeLists(work);
        count += addedTextures(files, work, log);
        byte[] vault = vaultScript(files, log);
        if (vault != null) {
            Files.write(new File(work, VAULT).toPath(), vault);
            count++;
        }
        Files.write(new File(work, MARK).toPath(), (count + "\n").getBytes(StandardCharsets.US_ASCII));
        File cars = new File(dir, "cars");
        File keepCars = new File(ModInstaller.modsDir(), ".cars_" + DIR);
        deleteTree(keepCars);
        if (cars.isDirectory() && !cars.renameTo(keepCars)) throw new IOException("cannot move " + cars);
        deleteTree(dir);
        if (!work.renameTo(dir)) throw new IOException("cannot move " + work + " to " + dir);
        if (keepCars.isDirectory() && !keepCars.renameTo(new File(dir, "cars"))) throw new IOException("cannot move " + keepCars);
        log.accept("unlimiter: " + count + " files");
        return count;
    }

    // the release's VltEd part (VLT/install.nfsms and the scripts it names, in its order) as one script: the frontend
    // nodes of the new menus with their cameras, which CarConverter's build applies to the game's vault
    private static byte[] vaultScript(Map<String, File> files, Consumer<String> log) throws IOException {
        Map<String, String> byLower = new java.util.HashMap<>();
        for (String p : files.keySet()) byLower.put(p.replace('\\', '/').toLowerCase(Locale.ROOT), p);
        String install = null;
        for (String l : byLower.keySet())
            if (l.equals(VAULT_INSTALL) || l.endsWith("/" + VAULT_INSTALL)) install = l;
        if (install == null) return null;
        StringBuilder out = new StringBuilder();
        int scripts = include(files, byLower, install, out, 0);
        log.accept("unlimiter: " + scripts + " vault scripts");
        return out.toString().getBytes(StandardCharsets.ISO_8859_1);
    }

    private static final java.util.regex.Pattern SCRIPT = java.util.regex.Pattern.compile("^\\s*script\\s+\"?([^\"]+?)\"?\\s*$");

    private static int include(Map<String, File> files, Map<String, String> byLower, String path, StringBuilder out, int depth)
            throws IOException {
        String key = byLower.get(path);
        if (key == null || depth > 8) return 0;
        String folder = path.substring(0, path.lastIndexOf('/') + 1);
        int scripts = 1;
        for (String line : new String(Files.readAllBytes(files.get(key).toPath()), StandardCharsets.ISO_8859_1).split("\r?\n")) {
            java.util.regex.Matcher m = SCRIPT.matcher(line);
            if (m.matches()) scripts += include(files, byLower, folder + m.group(1).replace('\\', '/').toLowerCase(Locale.ROOT), out, depth + 1);
            else out.append(line).append('\n');
        }
        return scripts;
    }

    // the release's textures for FrontB (the new menus' icons, VISUAL_PART_*) and GlobalB (TIRE_STYLE*, CARSHADOW_NEON*),
    // which its Binary part binds into those packs: in the added textures' form (CarConverter.extrasFiles), which
    // GetTextureInfo hands out (src/unlimiter/assets.cpp) with no game files made again
    private static int addedTextures(Map<String, File> files, File work, Consumer<String> log) throws IOException {
        StringBuilder list = new StringBuilder();
        java.util.Set<Integer> seen = new java.util.HashSet<>();
        File dir = new File(work, CarConverter.ADDED_DIR);
        List<String> paths = new java.util.ArrayList<>(files.keySet());
        java.util.Collections.sort(paths);
        for (String p : paths) {
            String l = p.replace('\\', '/').toLowerCase(Locale.ROOT);
            if (!l.endsWith(".dds") || !(l.contains("/textures/frontb/") || l.contains("/textures/globalb/"))) continue;
            String name = p.substring(p.replace('\\', '/').lastIndexOf('/') + 1, p.length() - 4).toUpperCase(Locale.ROOT);
            CarConverter.AddedTexture t = CarConverter.ddsTexture(Files.readAllBytes(files.get(p).toPath()), CarConverter.bhash(name));
            if (t == null || !seen.add(t.hash)) continue;
            if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("cannot write to " + dir);
            Files.write(new File(dir, String.format("%08x.dxt", t.hash)).toPath(), t.data);
            list.append(String.format("%08x %d %d %d\n", t.hash, t.width, t.height, t.format));
        }
        if (seen.isEmpty()) return 0;
        Files.write(new File(work, CarConverter.ADDED_LIST).toPath(), list.toString().getBytes(StandardCharsets.US_ASCII));
        log.accept("unlimiter: " + seen.size() + " textures");
        return seen.size();
    }

    // the vault script kept by install, or null
    static byte[] vault() {
        File f = new File(dir(), VAULT);
        try {
            return installed() && f.isFile() ? Files.readAllBytes(f.toPath()) : null;
        } catch (IOException e) {
            return null;
        }
    }

    private static void writeLists(File dir) throws IOException {
        File[] subs = dir.listFiles(File::isDirectory);
        if (subs == null) return;
        for (File sub : subs) {
            File[] inis = sub.listFiles((d, n) -> n.toLowerCase(Locale.ROOT).endsWith(".ini"));
            if (inis == null || inis.length == 0) continue;
            java.util.Arrays.sort(inis);
            StringBuilder list = new StringBuilder();
            for (File f : inis) list.append(f.getName()).append('\n');
            Files.write(new File(sub, "list.txt").toPath(), list.toString().getBytes(StandardCharsets.UTF_8));
        }
    }

    static void remove() {
        File dir = dir();
        File[] in = dir.listFiles();
        if (in == null) return;
        for (File f : in) if (!f.getName().equals("cars")) deleteTree(f);
    }

    private static void deleteTree(File f) {
        File[] in = f.listFiles();
        if (in != null) for (File c : in) deleteTree(c);
        f.delete();
    }
}
