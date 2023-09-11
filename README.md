Calugaritoiu Ion-Victor 332CA

Sisteme de Operare - Tema 2 - Memory Allocator

1. os_malloc:
    - functia verifica daca dimensiunea memoriei ceruta de utilizator este mai
    mica decat threshold-ul pentru mmap: 128 * 1024B; daca este mai mica, se
    utilizeaza functia sbrk; pentru size-uri mai mari se utilizeaza mmap;

    - in cazul primei alocari se aloca un chunk mai mare de memorie cu scopul
    de a fi split-uit in viitor si a reduce numarul de syscalls; prima alocare
    se verifica testand nulitatea head-ului listei de block-uri de memorie;

    - la fiecare apel se cauta un bloc de memorie disponibil pentru alocare
    iterand prin lista de block-uri; se verifica daca size-ul blocului gasit
    este suficient de mare pentru a acomoda dimensiunea primita in functie;

    - daca nu exista niciun bloc reutilizabil, se verifica daca ultimul bloc
    din lista are STATUS_FREE (a fost apelata functia free) si se aloca memorie
    in ultimul slot;

    - daca nu exista niciun slot disponibil se creeaza un nou bloc care va
    deveni tail-ul listei;

2. os_calloc:
    - functia apeleaza os_malloc si memset pentru a initializa cu 0;

3. os_realloc:
    - functia verifica daca noul size este mai mare decat capacitatea blocului,
    caz in care blocul poate fi extins (doar daca urmatorul bloc este free);

    - daca noul size este mai mic decat size-ul blocului original, blocul poate
    fi micsorat, astfel utilizand memoria in mod eficient;

    - daca blocul nu poate fi resized in place, se aloca memorie pentru un bloc
    nou in care se copiaza payload-ul blocului original; memoria alocata pentru
    cel original este freed;

4. os_free:
    - se itereaza prin lista utilizand 2 pointeri: curr & prev, utilizati pentru
    a realiza operatiile necesare stergerii unui bloc din lista;
    - pentru blocurile alocate cu mmap se apeleaza munmap;
    - dupa fiecare operatie de stergere se actualizeaza pointerii head / tail;
