
The Linux I/O Path
==================

  Script zur Vorlesung am 25.04.2019
    Andreas Grapentin

Vorwort
-------

Ich empfehle folgendes kurzes Einführungsvideo, in dem beschrieben wird, wie
die Einführung von UNIX die Welt der Computer /disruptiv/ verändert hat.

  "AT&T Archives: The UNIX Operating System" (1982):
  https://www.youtube.com/watch?v=tc4ROCJYbm0

Zu bemerken ist u.A. die Besetzung des Films mit einigen der großen Helden der
Betriebssystementwicklung: Brian Kernighan, Dennis Ritchie, Ken Thompson, ...

Inhalt dieses Dokuments
-----------------------

Ziel der in diesem Script beschrieben Vorlesung war es, eine Brücke zu schlagen
zwischen den am Beispiel von Windows vorgestellten konkreten Konzepten des I/O
Pfades im Betriebssystem und deren Gegenstücken in der UNIX Philosophie,
verdeutlicht am Beispiel des Linux Kernels.

Während der Vorlesung wurden Experimente im Debugger durchgeführt, um bestimmte
Verhaltensweisen des Systems aufzuzeigen. Die allgemeinen Konzepte wurden aus
den Beobachtungen der Experimente abgeleitet. Dieses Script soll als
Dokumentation dieser Experimente dienen, und es ermöglichen, sie selbständig
auszuführen und nachzuvollziehen. Dazu ist jeweils der Versuchsaufbau sowie die
erwarteten Ergebnisse und deren Erklärungen im Folgenden ausgeführt.

Der /rote Faden/ dieses Dokuments soll vom Beginn des I/O Prozesses in der
Anwendung bis hin zur Interaktion der Gerätetreiber mit der Hardware führen.

Im User Mode
------------

Am Anfang steht das Programm. Ein einfaches C Programm, was mit dem I/O
Funktionen der C Standardbibliothek interagiert, könnte zum Beispiel so
aussehen:

 | $> cat file_io.c
 | #include <stdio.h>
 |
 | int main(void) {
 |   FILE *f = fopen("out.txt", "w");
 |   fputs("hello, world\n", f);
 |   fclose(f);
 | }

Übersetzt man dieses Programm, und führt es aus, dann wird es erwartungsgemäß
eine Datei namens `out.txt' im Arbeitsverzeichnis des Prozesses erstellen,
deren Inhalt der Text "hello, world" ist:

 | $> gcc -o file_io file_io.c -g
 | $> ./file_io
 | $> cat out.txt
 | hello, world

Was hierbei genau passiert, ist in der C Standardbibliothek versteckt,
allerdings kann man die Kommunikation in den Kernel, der während der Ausführung
durch Systemaufrufe passiert mithilfe des Programms `strace' sichtbar machen:

 | $> strace ./file_io

`strace' ist ein Programm, welches alle vom untersuchten Programm aufgerufenen
Systemfunktionen, sowie deren Argumente und Rückgabewerte auf der Kommandozeile
ausgibt. Im konkreten Beispiel ist das eine lange Liste von Systemaufrufen, die
von der C Laufzeitumgebung erzeugt werden, bevor in die `main' Funktion
gesprungen wird, sowie am Ende der Ausgabe einige wenige Aufrufe, die unser
kleines I/O Programm erzeugt:

 | $> strace ./file_io
 | [...]
 | openat(AT_FDCWD, "out.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
 | fstat(3, {st_mode=S_IFREG|0644, st_size=0, ...}) = 0
 | write(3, "hello, world\n", 13)          = 13
 | close(3)                                = 0
 | [...]

Wir sehen dabei, dass die Funktionen des kleinen C Programms abgebildet werden
auf eine Menge von Systemaufrufen. Mit dem `openat' Systemaufruf wird die Datei
geöffnet, und erhält den /Dateideskriptor/ (fd) 3. Dateideskriptoren werden vom
Betriebssystem verwendet, um im Kontext eines Prozesses geöffnete Dateien, oder
Datei-ähnliche Objekte zu identifizieren. Mehr dazu später, wenn wir die
Aktivitäten im Kernel genauer betrachten. Nach erfolgtem Öffnen der Datei wird
mit dem `write' Systemaufruf auf fd 3 der Text "hello, world" geschrieben, und
mit `close' der fd geschlossen. Die Bedeutung der Parameter kann auf den
entsprechenden Manualseiten der Systemaufrufe nachgelesen werden:

 | $> man 2 <syscall name>

Genauer als mit `strace' lassen sich diese Effekte mit dem Debugger `gdb'
betrachten. Starten wir das Programm in `gdb', dann wird der Debugger die Debug
Symbole aus dem Programm laden, und uns eine Kommandozeile bereitstellen:

 | $> gdb ./file_io
 | [...]
 | Reading symbols from ./file_io...done.
 | (gdb)

Starten wir das Programm mit dem Befehl `start', wird der Debugger die
Programmausführung beginnen, bis die C Laufzeitumgebung die `main' Funktion
ausführt, und dort stoppen und auf weitere Anweisungen warten.

 | (gdb) start
 | [...]
 | Temporary breakpoint 1, main () at file_io.c:4
 | 4      FILE *f = fopen("out.txt", "w");
 | (gdb)

((Alternativ kann man mit dem `starti' Befehl die Programmausführung beginnen,
und an der ersten /Instruktion/, also dem Eintrittspunkt in das Programm der
sich in der C Laufzeitumgebung befindet, anhalten lassen. Dadurch könnte man
sich den Assembler code der Initialisierungsroutinen der Laufzeitumgebung
ansehen.))

Hat der Debugger die `main' Funktion erreicht, ist es Zeit, die Systemaufrufe
mit einem /Catchpoint/ -- eine Art generischer /Breakpoint/ -- zu
instrumentieren. Damit erreichen wir eine Unterbrechung des Programmablaufes,
sobald ein Systemaufruf an den Kernel abgeschickt wird, oder zurückkehrt.

 | (gdb) catch syscall
 | Catchpoint 2 (any syscall)

Setzen wir nun die Programmausführung mit dem Befehl `continue' (abgekürzt `c')
fort, dann wird der Debugger beim nächsten auftretenden Systemaufruf das
Programm anhalten und uns die Chance geben, den aktuellen Programmzustand zu
untersuchen:

 | (gdb) c
 | Continuing.
 |
 | Catchpoint 2 (call to syscall brk), 0x00007ffff7eb2b9b in brk () from /usr/lib/libc.so.6

Zuerst sehen wir, durch wiederholtes Ausführen des `continue' Befehls, zwei
`brk' Systemaufrufe. Diese Funkion wird für dynamische Speicherverwaltung
benutzt und ist ein Indiz dafür, dass die C Standardbibliothek dynamischen
Speicher zum Herstellen der zurückgegeben FILE* Zeiger in der `fopen' Funktion
verwendet.

Tatsächlich kann man sehen, wenn man sich mit dem Befehl `back' den
/Stacktrace/ des Prozesses anzeigen lässt, dass wir uns gerade in der
Ausführung von `fopen' (#7) befinden, und darin `malloc' (#6) aufgerufen wird.
Die Funktion `malloc' ist zum großen Teil in der C Standardbibliothek
implementiert, und greift auf den `brk' (#0) Systemaufruf zurück, um die Größe
des Heaps zu modifizieren. Damit hat sich die Vermutung also bestätigt:

 | (gdb) back
 | #0  0x00007ffff7eb2b9b in brk () from /usr/lib/libc.so.6
 | #1  0x00007ffff7eb2c81 in sbrk () from /usr/lib/libc.so.6
 | #2  0x00007ffff7e475ed in __default_morecore () from /usr/lib/libc.so.6
 | #3  0x00007ffff7e43a56 in sysmalloc () from /usr/lib/libc.so.6
 | #4  0x00007ffff7e44b9f in _int_malloc () from /usr/lib/libc.so.6
 | #5  0x00007ffff7e44dd5 in tcache_init.part () from /usr/lib/libc.so.6
 | #6  0x00007ffff7e45bd6 in malloc () from /usr/lib/libc.so.6
 | #7  0x00007ffff7e31580 in __fopen_internal () from /usr/lib/libc.so.6
 | #8  0x0000555555555164 in main () at test.c:4

Setzen wir die Programmausführung mit dem `continue' Befehl fort, erreichen wir
den Systemaufruf `openat'. Ein Stacktrace wird zeigen, dass wir noch immer die
Funktion `fopen' abarbeiten, was zu erwarten war. Danach erreichen wir den
Systemaufruf `fstat', welcher von der `fwrite' Funktion aufgerufen wird. Der
Systemaufruf `fstat' wird für einen gültigen Dateideskriptor die Eigenschaften
der dahinter liegenden Datei auslesen und zurückgeben. Diese Informationen
werden intern von der `fwrite' Funktion verwendet.

Setzen wir die Ausführung mit dem `continue' Befehl noch weiter fort, dann
passiert etwas nicht sofort trivial offensichtliches. Der Debugger wird wie
erwartet am nächsten Systemaufruf -- dem Systemaufruf `write' (#0) -- stoppen,
aber dieser Systemaufruf wurde von `fclose' (#5) ausgelöst, und nicht von
`fwrite'. Man kann also sehen, dass `fwrite' gar nicht notwendigerweise die zu
schreibenden Daten direkt dem Betriebssystem übergibt, sondern die Daten
zwischenpuffert:

 | Catchpoint 2 (call to syscall write), 0x00007ffff7eaccc8 in write () from /usr/lib/libc.so.6
 | (gdb) back
 | #0  0x00007ffff7eaccc8 in write () from /usr/lib/libc.so.6
 | #1  0x00007ffff7e3ce5d in _IO_file_write@@GLIBC_2.2.5 () from /usr/lib/libc.so.6
 | #2  0x00007ffff7e3c1bf in new_do_write () from /usr/lib/libc.so.6
 | #3  0x00007ffff7e3dfd9 in __GI__IO_do_write () from /usr/lib/libc.so.6
 | #4  0x00007ffff7e3d810 in __GI__IO_file_close_it () from /usr/lib/libc.so.6
 | #5  0x00007ffff7e30d6f in fclose@@GLIBC_2.2.5 () from /usr/lib/libc.so.6
 | #6  0x00005555555551a1 in main () at test.c:6

### Aufgabe ##################################################################
# Was passiert, wenn Sie den `fclose' Funktionsaufruf aus dem Programm
# entfernen?  Werden die Daten dennoch geschrieben? Und falls ja, wer wird den
# `write' Systemaufruf ausführen?
##############################################################################

An diesem Punkt scheint es für die Untersuchung des Systemverhaltens nützlich
zu sein, eine Abstraktionsschicht abzusteigen, und anstelle der `fopen' und
`fwrite' Funktionen die Funktionen `open' und `write' zu verwenden, die direkt
auf Dateideskriptoren agieren und nicht auf FILE* Zeigern. Das einfache C
Programm von vorher könnte dann wiefolgt aussehen:

 | $> cat sys_io.c
 | #include <fcntl.h>
 | #include <unistd.h>
 |
 | int main(void) {
 |   int fd = open("out.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
 |   const char *s = "hello, world\n";
 |   write(fd, s, strlen(s));
 |   close(fd);
 | }

Dieses Programm zeigt zu dem vorherigen Programm identisches beobachtbares
Verhalten. Wiederholen wir allerdings analog zu vorher die Untersuchungen mit
`strace' und `gdb', werden wir feststellen, dass die Abbildung zwischen den
aufgerufenen Funktionen in der C Standardbibliothek und den ausgelösten
Systemaufrufen viel direkter ist, und dass `write' auch tatsächlich einen
`write' Systemaufruf direkt zur Folge hat.

Ein Blick in den Kernel
-----------------------

Wir haben den Ablauf einer I/O Operation bis zur Grenze des Kernels verfolgt.
Als nächstes möchte ich zeigen, welche Informationen ein Prozess über seine
Kernel Datenstrukturen herausfinden kann, ohne tatsächlich in den Kernel Modus
wechseln zu müssen.

Dafür erweitern wir das Programm von vorhin insofern, dass es nach erfolgtem
Öffnen und Schreiben seiner Ausgabedatei -- aber vor dem Schließen dieser Datei
-- in einem Kindprozess eine Shell startet, und auf Beendigung dieser Shell
wartet. Dadurch erhalten wir Zugriff auf die zur Laufzeit des Prozesses
existierenden Datenstrukturen im `proc' Dateisystem, bis wir die gestartete
Shell wieder verlassen und der Prozess somit terminieren darf. Das angepasste C
Programm könnte wiefolgt aussehen:

 | $> cat proc_io.c
 | #include <fcntl.h>
 | #include <unistd.h>
 | #include <string.h>
 | #include <stdio.h>
 | #include <sys/wait.h>
 |
 | int main (void) {
 |   int fd = open("out.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
 |   const char *s = "hello, world\n";
 |   write(fd, s, strlen(s) + 1);
 |
 |   printf("%u\n", getpid());
 |
 |   pid_t pid = fork();
 |   if (pid == 0) // child
 |     execl("/usr/bin/bash", "bash", NULL);
 |   else // parent
 |     wait(NULL);
 | }

Das Programm wird außerdem seine /ProzessID/ (pid) mitteilen. Im Verzeichnis
`/proc/<pid>/' finden wir alle Informationen, die das Betriebssystem zu diesem
Prozess preisgibt. Von konkretem Interesse ist das Verzeichnis `/proc/<pid>/fd'

 | $> gcc -o proc_io proc_io.c -g
 | $> ./proc_io
 | $> ls -lh /proc/<pid>/fd
 | total 0
 | lrwx------ 1 nobody nobody 64 Apr 26 01:25 0 -> /dev/pts/9
 | lrwx------ 1 nobody nobody 64 Apr 26 01:25 1 -> /dev/pts/9
 | lrwx------ 1 nobody nobody 64 Apr 26 01:25 2 -> /dev/pts/9
 | lrwx------ 1 nobody nobody 64 Apr 26 01:25 3 -> /home/nobody/out.txt
 | $> exit

Hier ist erkennbar, dass die Dateideskriptoren 0, 1 und 2 des Prozesses auf das
Gerät `/dev/pts/9' verweisen, und der Dateideskriptor 3, wie vorhin bei
`strace' gezeigt, unserer geöffneten Datei zugewiesen ist. Die
Dateideskriptoren 0, 1 und 2 sind Standardwerte, die dem Prozess zur Startzeit
erstellt werden und auf das Terminal des Elternprozesses verweisen. Die Datei
`/dev/pts/9' ist ein sogenannter /Pseudoterminal Slave/, der verwendet wird um
ein Terminal Gerät bereitzustellen, was in einem graphischen Terminalemulator,
wie zum Beispiel `xterm' verwendet werden kann.

Die /echten/ Terminals des Systems sind limitiert, und nur außerhalb der
graphischen Benutzeroberfläche zu erreichen. Die entsprechenden Geräte befinden
sich im Dateisystem unter `/dev/tty*'.

### Aufgabe ##################################################################
# Was passiert, wenn Sie die Standardausgabe des Programms zur Startzeit auf
# eine Datei umleiten? Welche Informationen finden Sie dann im `proc'
# Dateisystem?
##############################################################################

### Aufgabe ##################################################################
# Führen Sie `proc_io' sowohl in einem "echten" Terminal, als auch in einem
# Pseudoterminal aus, und betrachten Sie die Dateideskriptorinformationen im
# `proc' Dateisystem.
##############################################################################

Hieran ist die UNIX Philosophie "Alles ist Datei" erkennbar. Die Aussage meint,
dass sich Dateien und Geräteschnittstellen einen Namensraum im Dateisystem
teilen, und dass ich alle Objekte in meinem Dateisystem mit der gleichen `open'
Funktion an einen Dateideskriptor binden kann.

Das Programm `stat' ist in der Lage, zu unterscheiden um was für eine Art von
Objekt sich hinter einem Namen im Dateisystem verbirgt. Zum Beispiel:

 | $> stat /dev/zero
 |   File: /dev/zero
 |   Size: 0            Blocks: 0          IO Block: 4096   character special file
 |   [...]
 | $> stat /dev/loop0
 |   File: /dev/loop0
 |   Size: 0            Blocks: 0          IO Block: 4096   block special file
 |   [...]
 | $> stat /etc/passwd
 |   File: /etc/passwd
 |   Size: 1689         Blocks: 8          IO Block: 4096   regular file

Weitere mögliche typen sind /sockets/, /symlinks/, /directories/ und /FIFOs/.
(Siehe `man 7 inode')

Wir haben im `proc' Dateisystem `/proc/<pid>/fd' betrachtet, interessant ist
außerdem noch `/proc/<pid>/fdinfo/':

 | $> ./proc_io
 | $> cat /proc/<pid>/fdinfo/3
 | pos: 14
 | flags:       0100002
 | mnt_id:      118
 | $> exit

Die Felder in dieser Datei -- wie auch für viele weitere Knoten im `proc'
Dateisystem sind in der Manualseite für `procfs' (`man 5 procfs') beschrieben.
Interessant ist das `mnt_id' Feld, dieses verweist auf einen Eintag in der
`mountinfo' des Prozesses (`/proc/<pid>/mountinfo'):

 | $> ./proc_io
 | $> cat /proc/<pid>/mountinfo | grep 118
 | 118 26 8:5 / /home rw,relatime shared:65 - ext4 /dev/sda5 rw,data=ordered
 | $> exit

Der Verweis in die `mountinfo' Struktur macht sichtbar, auf welchem Gerät sich
die Datei befindet, die sich hinter dem Dateideskriptor verbirgt. In diesem
konkreten Fall betrachten wir eine reguläre Datei auf der Partition /dev/sda5,
die in /home eingebunden ist. Außerdem ist sichtbar, dass dem Gerät /dev/sda5
die Identifikationsnummer 8:5 zugewiesen ist. Das kann auch das `stat' Programm
ausgeben:

 | $> stat /dev/sda5
 |   File: /dev/sda5
 |   Size: 0            Blocks: 0          IO Block: 4096   block special file
 | Device: 6h/6d        Inode: 13499       Links: 1     Device type: 8,5
 | [...]

Hier ist ersichtlich, dass /dev/sda5 ein Block Special File ist, dessen Typ
durch die Major Nummer 8 und die Minor Nummer 5 beschrieben ist. Die Major
Nummer 8 bedeutet, dass es sich bei dem Block Gerät um eine Festplatte am SCSI
Bus handelt, und die Minor Nummer 5 identifiziert die fünfte Partition des
ersten Gerätes an diesem Bus. Diese Kombinationen aus Major und Minor Nummern
beschreiben, welcher Treiber für Interaktion mit diesem Block oder Character
Special File zuständig ist. Eine Beschreibung aller möglicher Kombinationen ist
hier zu finden:
https://www.kernel.org/doc/Documentation/admin-guide/devices.txt

Wie genau diese Zuordnung funktioniert, als allerdings aus dem `proc'
Dateisystem nicht ersichtlich. Dazu müssen wir in den Kern selbst schauen.

Im Kernel Mode
--------------

Der Kernel ist ein Programm, also kann man ihn debuggen. Problematisch dabei
ist, dass man ihn nicht wie gewohnt durch setzen eines Breakpoints anhalten
kann, wenn er den Debugger selber hostet, weil dies auch den Debugger zum
Stillstand bringen würde.

Die Lösung für dieses Problem ist, dass man den Kernel-Debugger nicht auf dem
System laufen lässt, auf dem auch der Kernel debuggt wird, sondern auf einem
zweiten System, und diese Systeme über eine serielle Schnittstelle miteinander
verbindet. Auf diese Weise kann man den Kernel sicher anhalten ohne Kontrolle
über den Debugger zu verlieren. Noch mehr Freiheitsgrade beim Debuggen hat man,
wenn das zu debuggende System in einer Virtuellen Maschine läuft, und der
Debugger damit uneingeschränkten Zugriff auf die emulierten Hardwareressourcen
bekommen kann.

Einen sehr schnellen Einstieg bietet das `linux-kernel-module-cheat' Projekt
auf github:

https://github.com/cirosantilli/linux-kernel-module-cheat

Es enthält eine Menge von Scripts und eine vorkonfigurierte `buildroot'
Umgebung, die einen auf Debugging optimierten Kernel, sowie eine einfache
Laufzeitumgebung erzeugt.

Die Komponenten werden dabei automatisch vorbereitet und übersetzt:

 | $> git clone https://github.com/cirosantilli/linux-kernel-module-cheat
 | $> cd linux-kernel-module-cheat
 | $> ./build --download-dependencies qemu-buildroot

Diese Anweisung sind aus dem README des Projektes entnommen und müssen
möglicherweise für neuere als meine verwendete Version des Projekts
entsprechend angepasst werden. Ich empfehle, die "Getting Started" Sektion des
READMEs zu lesen. Die von mir verwendete Version des Projekts ist:

 | $> git rev-parse HEAD
 | 550897ce1766e8df0b4ffcfdc17206f788d9f67f

Auf sehr neuen System kann es sein, dass für den Kernel sowie den
Systememulator `qemu' Patches benötigt werden. Für Linux wird möglicherweise
folgender Patch benötigt, falls beim Debuggen Probleme mit undefinierten MS_*
Symbolen auftreten:

https://www.spinics.net/lists/stable/msg290600.html

Und falls beim Übersetzen von `qemu' Fehler bei der Verwendung von `glusterfs'
auftreten, leisten folgende Patches abhilfe:

https://git.qemu.org/?p=qemu.git;a=patch;h=e014dbe74e0484188164c61ff6843f8a04a8cb9d
https://git.qemu.org/?p=qemu.git;a=patch;h=0e3b891fefacc0e49f3c8ffa3a753b69eb7214d2

Auf Ubuntu Systemen sind diese Patches üblicherweise nicht nötig, auf meinem
Archlinux System waren sie relevant. Die Scripts erwarten außerdem das
`apt-get' Programm um wenige Systemweite Abhängigkeiten zu installieren. Auf
Distributionen mit anderen Paketmanagern rate ich, ein /usr/local/bin/apt-get
zu erstellen, mit folgendem Inhalt:

 | $> cat /usr/local/bin/apt-get
 | #!/bin/sh
 | true

Die Aufrufe an apt-get werden von den Scripts auf der Konsole ausgegeben, und
sicherzustellen, dass die überschaubare Menge von Abhängigkeiten installiert
ist, ist trivial.

Ist die Umgebung erfolgreich Übersetzt, wird die virtuelle Maschine mit
folgendem Kommando im angehaltenen Zustand gestartet:

 | $> ./run --wait-gdb

Um die laufende virtuelle Maschine zu Beenden, muss -- wie von `qemu' gewohnt
-- auf der Kommandozeile die Tastenkombination Ctrl+A gedrückt werden, gefolgt
von der Taste x.

Der Debugger wird in einem zweiten Terminal gestartet:

 | $> ./run-gdb start_kernel

woraufhin Sowohl Kernel als auch Debugger in Tandem die Ausführung beginnen,
und den Kernel bis zum Beginn der `start_kernel' Funktion ausführen, und dort
die Ausführung anhalten.

 | $> ./run-gdb start_kernel
 | [...]
 | Remote debugging using localhost:45457
 | [...]
 | Breakpoint 1, start_kernel ()
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/init/main.c:538
 | 538  {
 | loading vmlinux
 | (gdb)

Ab hier funktionieren die `gdb' Befehle sehr ähnlich zu den aus dem Debugging
von User Modus Prozessen bekannten Befehlen. Ich kann empfehlen, sich die
Ausführung von `start_kernel' und den weiter aufgerufenen Funktionen genauer
anzusehen. Eine übersichtliche Kurzanleitung zu `gdb' ist zum Beispiel hier zu
finden:

https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf

Für uns von Interesse ist allerdings der I/O Pfad durch den Kernel. Dazu müssen
wir zuerst das System fertig booten lassen, was mit dem Befehl `continue'
erreicht wird. Der Befehl `continue' wird dabei /blocken/, was bedeutet, dass
der Debugger keine weiteren Befehle entgegen nehmen wird, während das debuggte
System läuft.

 | (gdb) c
 | Continuing.

Im anderen Terminal wird nun die Ausgabe des hochfahrenden Systems erscheinen,
bis es eine Shell präsentiert.

 | [...]
 | <6>[    9.249814] Run /sbin/init as init process
 | hello S98
 | # echo $SHELL
 | /bin/sh
 | #

An diesem Punkt können wir das laufende System mit einem beherzten Ctrl+C im
Debugger unterbrechen, um wieder Zugriff auf die Kommandozeile des Debuggers zu
erhalten:

 | ^C
 | Program received signal SIGINT, Interrupt.
 | default_idle ()
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/arch/x86/kernel/process.c:565
 | 565          trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
 | (gdb)

Wir haben bisher den Ablauf einer I/O Operation -- also den Verlauf des
Kontrollflusses der Funktionen fopen/open, sowie fputs/write -- bis hin zum
Moduswechsel in den Kernel Modus durch einen Systemaufruf betrachtet. Jetzt im
Kernel Debugger können wir den Verlauf der Systemaktivität /nach/ dem Eintritt
in den Kernel weiter verfolgen. Dazu müssen wir taktisch sinnvolle Breakpoints
definieren, um die Ausführung des Kernels an passenden Stellen zu unterbrechen.

Im Quellcode des Linux Kernes in `arch/x86/include/asm/syscall_wrapper.h' Zeile
51ff ist erkennen, dass bei Definition eines Systemaufrufs im Kern die
Eintrittsfunktion in den Kern den Namen `__x64_sys_<syscall>' erhält. Dieses
Wissen machen wir uns zunutze, und erstellen einen Breakpoint beim
Eintrittspunkt des `openat' Systemaufrufs wiefolgt:

 | (gdb) b __x64_sys_openat
 | Breakpoint 2 at 0xffffffff81195290: file /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c, line 1084.
 | (gdb)

Setzen wir die Ausführung des durch den Befehl `continue' fort, wird nun bei
jedem zukünftigen Aufruf des Systemaufrufs `openat' die Ausführung unterbrochen
werden. Dies ist erkennbar, wenn man das Programm `true' in der Kommandozeile
ausführt:

 | # true

Wenn alles richtig funktioniert, wird der Debugger die Ausführung mit folgender
Ausgabe unterbrechen:

 | Breakpoint 2, __x64_sys_openat (regs=0xffffc90000133f58)
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c:1084
 | 1084 SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags,
 | (gdb)

Der Backtrace dieser Funktion ist interessant, und zeigt auf, in welchen
Funktionen die Interrupt Behandlung in Linux passiert:

 | (gdb) backtrace
 | #0  __x64_sys_openat (regs=0xffffc90000133f58)
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c:1084
 | #1  0xffffffff8100214d in do_syscall_64 (nr=0, regs=0xffffc90000133f58)
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/arch/x86/entry/common.c:290
 | #2  0xffffffff81800091 in entry_SYSCALL_64 ()
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/arch/x86/entry/entry_64.S:175
 | #3  0x0000000000000000 in ?? ()
 | (gdb)

Ich kann empfehlen, sich diese Funktionen mal anzusehen.

Wir betrachten jetzt weiter die Funktion `__x64_sys_openat'. Mit dem `list'
Befehl können wir uns den Quellcode der Funktion anzeigen lassen:

 | (gdb) list __x64_sys_openat
 | [...]
 | 1084 SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags,
 | 1085                 umode_t, mode)
 | 1086 {
 | 1087         if (force_o_largefile())
 | 1088                 flags |= O_LARGEFILE;
 | (gdb) list
 | 1089
 | 1090         return do_sys_open(dfd, filename, flags, mode);
 | 1091 }
 | [...]
 | (gdb)

Wie man leicht sieht, ist __x64_sys_openat eine Wrapperfunktion für
`do_sys_open', die wir näher betrachten wollen. Dafür setzen wir einen neuen
Breakpoint und deaktivieren den alten:

 | (gdb) b do_sys_open
 | Breakpoint 3 at 0xffffffff81195000: file /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c, line 1049.
 | (gdb) disable 2
 | (gdb)

Setzen wir jetzt die Ausführung erneut durch Benutzen des `continue' Befehls
fort, erhalten wir bei Erreichen des Breakpoints sehr viel nützlichere
Informationen:

 | (gdb) c
 | Continuing.
 |
 | Breakpoint 3, do_sys_open (dfd=-100, filename=0x6a64a9 "/.ash_history", flags=33857, mode=384)
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c:1049
 | 1049 {
 | (gdb) c
 | Continuing.
 |
 | Breakpoint 3, do_sys_open (dfd=-100, filename=0x7ffff76033e9 "/etc/passwd", flags=557056, mode=0)
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c:1049
 | 1049 {
 | (gdb)

Wir sehen, dass beim Start des `true' Programms von der Shell zwei Dateien
geöffnet werden, und zwar `/.ash_history' und `/etc/passwd'. Wir ignorieren
beide dieser Instanzen von `open' und führen stattdessen in der Shell folgendes
Kommando aus, um eine Datei nicht nur zu öffnen, sondern auch zu schreiben, und
unser C Programm zu simulieren, welches wir im User Modus genau betrachtet
hatten:

 | # echo 'hello, world' > out.txt

Der Debugger wird die Ausführung wieder anhalten, und wir fahren solange mit
der Ausführung durch Verwendung des `continue' Befehls fort, bis der `filename'
Parameter der `do_sys_open' Funktion unserer Datei `out.txt' entspricht:

 | (gdb) c
 | [...]
 | (gdb) c
 | Continuing.
 |
 | Breakpoint 3, do_sys_open (dfd=-100, filename=0x6a26b8 "out.txt", flags=33345, mode=438)
 |     at /home/nobody/linux-kernel-module-cheat/submodules/linux/fs/open.c:1049
 | 1049 {
 | (gdb)

Den Quellcode der do_sys_open Funktion erhält man wieder wahlweise durch
Verwendung des `list' Befehls im Debugger, oder durch direktes Öffnen der Datei
`fs/open.c' im Linux Kernel. Es folgt eine von mir kommentierte Version zum
Verständnis:

 | long do_sys_open(int dfd, const char __user *filename, int flags, umode_t mode)
 | {
 |         /* interpretiere die gegebenen `flags' und erzeuge eine
 |          * `struct open_flags' Struktur (vgl. `ptype struct open_flags' im
 |          * Debugger) */
 |         struct open_flags op;
 |         int fd = build_open_flags(flags, mode, &op);
 |         struct filename *tmp;
 |
 |         /* falls `build_open_flags' fehlschlägt, ist fd != 0 und enthält
 |          * den Fehlercode, der hier direkt als Rückgabewert dient. */
 |         if (fd)
 |                 return fd;
 |
 |         /* interpretiere den gegebenen Dateinamen in eine `struct filename'
 |          * Struktur (vgl. `ptype struct filename') */
 |         tmp = getname(filename);
 |         if (IS_ERR(tmp))
 |                 return PTR_ERR(tmp);
 |
 |         /* generiere den nächsten freien Dateideskriptor für den laufenden
 |            task -- Werte kleiner als 0 zeigen Fehler an */
 |         fd = get_unused_fd_flags(flags);
 |         if (fd >= 0) {
 |                 /* versuche die Datei zu öffnen, und behandle Fehler */
 |                 struct file *f = do_filp_open(dfd, tmp, &op);
 |                 if (IS_ERR(f)) {
 |                         put_unused_fd(fd);
 |                         fd = PTR_ERR(f);
 |                 } else {
 |                         fsnotify_open(f);
 |                         fd_install(fd, f);
 |                 }
 |         }
 |         putname(tmp);
 |         return fd;
 | }

Der Funktionsaufruf `do_filp_open' erzeugt eine `struct file' Struktur, welche
viele Interessante Details zu der geöffneten Datei, sowie weitere Verweise auf
Kernelstrukturen des I/O Systems (vgl. `ptype struct file') enthält. Um die
Ausführung bis in diese interessante Funktion fortzuführen, nutzt man die
Befehle `next' (Step Over) und `step' (Step Into), bis der Funktionsaufruf
`do_filp_open' erreicht ist, und betritt dann mit dem Befehl `step' die
Funktion. Altenativ bietet es sich an, durch den Befehl `b do_filp_open' einen
weiteren Breakpoint zu setzen.

Der Inhalt der Funktion `do_filp_open' ist überschaubar. Als einzig
interessante Unterfunktion lässt sich `path_openat' identifizieren.

Ab hier wird alles etwas unübersichtlicher, und die Aufgaben der einzelnen
Funktionen weniger offensichtlich. Durch schrittweises (`step' Befehl)
Fortsetzen der Kernelausführung lässt sich hier gut beobachten, was im Kernel
passiert. Nach einer Reihe weiterer Funktionen, die unter anderem Kernel
Objekte allozieren, Pfade traversieren, und Berechtigungen prüfen, erreicht der
Kontrollfluss die Funktionen des Dateisystemtreibers, wie z.B.
`ext4_find_entry'.

 | (gdb) s
 | [...]
 | (gdb) s
 | ext4_find_entry (dir=0xffff88800ea1e0e8, d_name=0xffff88800ea463e0, res_dir=0xffffc90000133c30,
 |     inlined=0x0 <irq_stack_union>)
 |     at /home/andi/linux-kernel-module-cheat/submodules/linux/fs/ext4/namei.c:1344
 | (gdb) back
 | #0  ext4_find_entry (dir=0xffff88800ea1e0e8, d_name=0xffff88800ea463e0,
 |     res_dir=0xffffc90000133c30, inlined=0x0 <irq_stack_union>)
 |     at /home/andi/linux-kernel-module-cheat/submodules/linux/fs/ext4/namei.c:1358
 | [...]
 | #4  path_openat (nd=0xffffc90000133d40, op=0xffffc90000133e7c, flags=<optimized out>)
 |     at /home/andi/linux-kernel-module-cheat/submodules/linux/fs/namei.c:3533
 | #5  0xffffffff811a762b in do_filp_open (dfd=<optimized out>, pathname=<optimized out>,
 |     op=0xffffc90000133e7c)
 |     at /home/andi/linux-kernel-module-cheat/submodules/linux/fs/namei.c:3564
 | [...]
 | (gdb) p *d_name
 | $2 = {{{hash = 3517636780, len = 7}, hash_len = 33582407852}, name = 0xffff88800ea463f8 "out.txt"}
 | (gdb)

Die Funktion `ext4_find_entry' wird daraufhin die Dateisystem Caching
Funktionen ansprechen, die durch die Speicherverwaltung des Kernels zu
verfolgen sind (Siehe z.B. `__getblk_gfp'), um festzustellen, ob das aktuelle
Verzeichnis die Datei `out.txt' enthält. Stept man weiter durch den Code, kehrt
man nach kurzer Zeit in den ext4 Treiber zurück, welcher dann versucht, die
Datei anzulegen (`ext4_create'). Tatsächliche Zugriffe auf die Festplatte
verbergen sich hier hinter der Virtuellen Speicherverwaltung und nichtlinearen
Kontrollflussstrukturen, die sich aus den Interrupts der Festplatte ergeben.

Ein ähnliches Bild zeigt sich, wenn man die Untersuchungen mit dem `write'
Systemaufruf startet, also den ersten Breakpoint in der Funktion
`__x64_sys_write' setzt.

Zusammenfassung
---------------

Es zeigt sich, dass der Kontrollfluss einer I/O Operation im User Mode zwischen
Windows und Linux recht ähnlich ist, nur die Vokabeln heißen anders. Im Kernel
Modus sind die Welten leicht Verschieden. Während Windows ein asynchrones
Verfahren über DPCs und APCs implementiert, arbeitet der Linux Kern synchron
die I/O Operationen ab. Die Treiber Stapel beider Systeme sind ähnlich
vielschichtig, sodass viele Ebenen des Kernels miteinander kooperieren müssen,
um eine I/O Operation abzuschließen.

Weiterführende Ressourcen
-------------------------

Die Betrachtungen in diesem Script bezogen sich auf Linux. Die meisten der
beschriebenen Konzepte stammen allerdings aus UNIX und haben sich in den
letzten 50 Jahren nur wenig verändert. Die Quellen von UNIX SystemV, sowie ein
installierbares System findet man heute auf archive.org:

  https://archive.org/download/ATTUNIXSystemVRelease4Version2

Die /Unix Heritage Society/ verwaltet ein Archiv alter Unix Versionen mit
Dokumentation und lauffähigen Images:

  https://www.tuhs.org/

Abhandlung über Linux Gerätetreiber auf lwn.net mit mehr Linux Treiber Details:

  http://static.lwn.net/images/pdf/LDD3/ch$i.pdf for i in {1..18}

gdb cheat sheet:

  https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf


==============================================================================
Copyright 2019 Andreas Grapentin <andreas.grapentin@hpi.uni-potsdam.de>

Dieses Dokument ist veröffentlicht unter der Creative Commons Attribution 4.0
International License (CC-by) Lizenz.

http://creativecommons.org/licenses/by/4.0/
