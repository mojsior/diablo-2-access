# D2 Access – instrukcja obsługi

D2 Access to mod dostępności do klasycznego Diablo II (Game.exe w wersji 1.14b) dla osób niewidomych i słabowidzących. Mod czyta na głos menu, postać, ekwipunek, drzewko umiejętności i otoczenie. Prowadzi też do wybranych celów, tak jak mod Diablo Access do pierwszego Diablo.

Mowa idzie przez NVDA. Gdy NVDA nie działa, mod używa syntezatora Windows (SAPI).

## Instalacja gry Diablo II

Mod wymaga oryginalnego Diablo II z dodatkiem Lord of Destruction w wersji 1.14b. Obie części można kupić w sklepie Battle.net jako „Diablo II” i „Diablo II: Lord of Destruction”; to nie jest Diablo II: Resurrected. Po zakupie Blizzard udostępnia pobieracz, który ściąga instalator do folderu, np. `D2-1.14b-Installer-plPL` z plikami `Installer.exe` i `Installer Tome.mpq`.

Instalator Blizzarda ma graficzne menu i okno umowy licencyjnej, którego nie da się zaakceptować samą klawiaturą. Dlatego do moda jest asystent instalacji `D2AccessSetup.exe`. Przeprowadza on przez instalację gry, a potem sam pobiera i instaluje moda.

### Instalacja z asystentem

1. Pobierz plik `D2AccessSetup.exe` z najnowszego wydania na stronie [Releases](https://github.com/mojsior/diablo-2-access/releases). To jeden plik, nie trzeba go rozpakowywać.
2. Uruchom `D2AccessSetup.exe`. Jeśli Windows zapyta o uprawnienia administratora, zgódź się, bo instalator Blizzarda i kopiowanie do folderu gry tego wymagają.
3. Asystent sam szuka instalatora gry w folderach Pobrane i Pulpit. Gdy znajdzie kilka, poprosi o wybór cyfrą; gdy nie znajdzie żadnego, otworzy okno wyboru pliku, w którym wskazujesz `Installer.exe`. Ścieżkę można też podać jako parametr: `D2AccessSetup.exe "D:\Pobrane\D2-1.14b-Installer-plPL"`.
4. Asystent uruchamia instalator i sam wybiera w graficznym menu instalację gry.
5. Przy umowie licencyjnej asystent pyta: Enter akceptuje umowę, Escape ją odrzuca. Po akceptacji sam przewija umowę i naciska „Akceptuję”.
6. Każde kolejne okno (klucz CD, wybór folderu, DirectX, komunikaty o błędach) asystent czyta na głos i przenosi na nie fokus. Klucz wpisujesz sam: najpierw imię właściciela, potem Tab i 26-znakowy klucz, na końcu Enter.
7. Podczas kopiowania plików asystent co 10% podaje postęp, a na końcu mówi, że instalacja gry się zakończyła, i zamyka instalator.
8. Asystent pyta, czy zainstalować moda. Po Enterze pobiera z GitHuba najnowsze wydanie Diablo 2 Access, rozpakowuje je i kopiuje do folderu, w którym zainstalowała się gra (zwykle `C:\Program Files (x86)\Diablo II`).
9. Jeśli masz dodatek Lord of Destruction, uruchom asystenta jeszcze raz i wskaż instalator dodatku.

### Aktualizacja moda

Gdy Diablo II jest już zainstalowane, asystent zaraz po uruchomieniu to wykrywa. Enter instaluje albo aktualizuje wtedy tylko moda do najnowszej wersji z GitHuba, a Escape mimo to uruchamia instalator gry. Przed aktualizacją zamknij grę.

Asystent sprawdzono z polskim i angielskim instalatorem Diablo II aż do okna klucza CD oraz pobieranie i instalację moda do folderu gry. Wybór folderu, postęp i zakończenie instalacji gry oraz instalator dodatku Lord of Destruction nie były jeszcze sprawdzone w praktyce. Jeśli coś pójdzie nie tak, dołącz do zgłoszenia plik `D2AccessSetup.log`, który powstaje obok asystenta.

### Skróty instalatora bez asystenta

Graficzne menu instalatora reaguje na klawisze, choć czytnik ekranu go nie widzi:

- D: zainstaluj grę;
- G (w wersji angielskiej P): graj, gdy gra jest już zainstalowana;
- O (w wersji angielskiej U): odinstaluj;
- W (w wersji angielskiej B): wstecz;
- Z (w wersji angielskiej X): zakończ instalator.

Okna klucza CD, folderu i DirectX to zwykłe okna Windows obsługiwane Tabem i Enterem. Jedynie umowy licencyjnej nie da się zaakceptować klawiaturą, bo przycisk „Akceptuję” włącza się dopiero po przewinięciu tekstu myszą.

## Uruchamianie

1. Uruchom `D2AccessLauncher.exe` z folderu gry, do którego asystent zainstalował moda. Launcher używa `Game.exe` leżącego obok siebie; gdy go nie ma, szuka gry w rejestrze, a na końcu w `C:\Program Files (x86)\Diablo II`.
2. Ścieżkę do gry można też podać jako parametr, na przykład: `D2AccessLauncher.exe "D:\Gry\Diablo II\Game.exe"`.
3. Po chwili usłyszysz „Menu główne” i nazwę zaznaczonej opcji.

Okno gry musi być aktywne (na pierwszym planie), inaczej gra nie przyjmuje klawiszy.

### Nie aktualizuj gry — mod działa z wersją 1.14b

Mod podpina się pod Diablo II 1.14b (wersja pliku 1.14.1.68) pod stałymi adresami, dlatego launcher sprawdza wersję `Game.exe` i przy innej odmawia uruchomienia, zamiast doprowadzić do awarii gry.

**Nie wybieraj opcji BATTLE.NET w menu głównym.** Uruchamia ona aktualizator Blizzarda, który kasuje plik `patch_d2.mpq`, po czym przerywa pracę na `binkw32.dll` i zostawia instalację wysypującą się przy starcie komunikatem „Diablo II Exception: ACCESS_VIOLATION”. Gra przez Battle.net i tak wymaga wersji 1.14d, której mod nie obsługuje. Ręczne uruchomienie `BNUpdate.exe` również nie pomaga: Blizzard nie udostępnia już tych plików, więc aktualizator zgłasza tylko brakujący plik.

Po instalacji moda `D2AccessSetup.exe` zmienia nazwę `BNUpdate.exe` na `BNUpdate.exe.disabled`, żeby gra nie mogła sama się zepsuć. Gdybyś kiedyś chciał przywrócić aktualizator, usuń końcówkę `.disabled`.

Jeżeli aktualizator już się uruchomił i gra przestała startować, skopiuj plik `patch_d2.mpq` (około 8 MB) z powrotem do folderu gry — z nośnika instalacyjnego albo z innej kopii gry.

## Język

Mod mówi po polsku albo po angielsku. Domyślnie wybiera język zainstalowanej gry: polska gra to polska mowa, angielska gra to angielska mowa. Nazwy z samej gry, na przykład przedmioty, lokacje i umiejętności, zawsze są w języku gry.

Język można wymusić plikiem `D2Access.ini` w folderze moda (obok `D2AccessHook.dll`):

```
[General]
Language=pl
```

Dozwolone wartości:

- `auto` – według języka gry (domyślnie)
- `pl` – zawsze po polsku
- `en` – zawsze po angielsku

## Menu główne

- Strzałka w górę i w dół: wybór opcji. Mod czyta nazwę i pozycję, na przykład „JEDEN GRACZ, 1 z 7”.
- Enter albo Spacja: zatwierdzenie opcji.
- 1: od razu gra jednoosobowa.

Dostępne opcje to kolejno: gra jednoosobowa, Battle.net, brama Battle.net, inne tryby wieloosobowe, twórcy, filmy i wyjście z gry. Opcja niedostępna w danej instalacji jest oznaczona słowem „niedostępne”.

## Wybór i tworzenie postaci

Wybór postaci:

- Strzałki: wybór postaci. Mod czyta imię, klasę, poziom i numer na liście.
- Enter: rozpoczęcie gry.
- N: tworzenie nowej postaci.
- Delete: usunięcie zaznaczonej postaci. Mod pyta o potwierdzenie: Enter usuwa, Escape anuluje. Usunięcia nie można cofnąć.

Tworzenie postaci:

- Strzałki w lewo i w prawo (albo w górę i w dół): zmiana klasy.
- Klawisze od 1 do 5: bezpośredni wybór klasy.
- Wpisz imię z klawiatury.
- Enter: utworzenie postaci. Jeśli imię jest błędne albo zajęte, mod to powie.

## Poruszanie się w grze

- Strzałki albo klawiatura numeryczna: ruch postaci. Jedno naciśnięcie to jeden krok (jeden kafel), a przytrzymanie idzie krok za krokiem. Dwie strzałki naraz dają krok po skosie. Gdy droga jest zablokowana, postać stoi i mod mówi „Ściana”.
- F1: pomoc z listą klawiszy.
- L: nazwa lokacji.
- K: współrzędne postaci.
- G albo F2: skan poziomu, czyli ile jest w pobliżu celów każdej kategorii.

## Cele i prowadzenie do celu

Mod dzieli otoczenie na kategorie: przedmioty, skrzynie, drzwi, kapliczki, obiekty, obiekty do zniszczenia, potwory, NPC, gracze, wyjścia, waypointy i portale.

- Control + Page Down / Control + Page Up: następna lub poprzednia kategoria.
- Page Down / Page Up: następny lub poprzedni cel w kategorii, od najbliższego.
- Home: opis drogi do celu w krokach, na przykład „północ 3, wschód 2”. Oznacza to trzy naciśnięcia strzałki w górę, a potem dwa w prawo (północ to strzałka w górę, wschód w prawo). Po przejściu części drogi naciśnij Home ponownie. Tę samą funkcję mają H i 5 na klawiaturze numerycznej.
- Shift + Home: automatyczny marsz do celu. Ponowne naciśnięcie zatrzymuje marsz. Przy wyjściach mod sam przechodzi do następnej lokacji.
- Control + Home: wyczyszczenie wybranego celu.
- E: interakcja z celem, na przykład otwarcie skrzyni, rozmowa z NPC, podniesienie przedmiotu czy użycie waypointu.
- F: atak na najbliższego potwora — zwykły albo wybraną umiejętnością bojową.
- S: następna umiejętność bojowa. Shift + S: poprzednia. Control + S: powrót do zwykłego ataku.

### Umiejętności bojowe (S)

Klawisz S przechodzi po umiejętnościach, które postać już zna, i ustawia wybraną do ataku. Mod czyta nazwę, poziom i pozycję na liście, na przykład „Zamach, poziom 1. 1 z 2. F używa tej umiejętności”. Potem każde naciśnięcie F atakuje wskazanego potwora tą umiejętnością, a Control + S wraca do zwykłego ataku bronią.

Na liście są tylko umiejętności, w które włożono punkt (albo dodają je przedmioty) i które da się trzymać w ręce. Umiejętności bierne i aury, jak mistrzostwa broni czy aury paladyna, działają same z siebie i gra nie pozwala ich wybrać, więc mod ich nie proponuje. Punkty rozdajesz w drzewku pod klawiszem T.

Gdy postać nie zna jeszcze żadnej umiejętności, mod powie „Nie masz jeszcze umiejętności do wyboru".

Gdy obok postaci pojawi się przedmiot, na przykład wypadnie ze skrzyni albo z potwora, mod powie „Na ziemi:” i nazwę. Takie przedmioty są w kategorii „przedmioty”.

### Dźwięki w otoczeniu

Dźwięki działają tak samo jak w Diablo Access:

- Mod gra naraz najwyżej trzy dźwięki najbliższych obiektów w promieniu 12 kroków.
- Każdy dźwięk powtarza się tym szybciej, im bliżej jesteś. Z daleka co sekundę, z bliska co ćwierć sekundy, a potwory nawet co 0,1 sekundy.
- Głośność maleje z odległością.
- Dźwięk przesuwa się w lewo lub w prawo tak, jak obiekt leży względem postaci na ekranie.
- Przedmioty mają osobne dźwięki: broń, zbroja (także tarcze), złoto, mikstury i zwoje. Słychać je wszędzie, także w mieście.
- Skrzynie, drzwi, wyjścia i potwory słychać tylko poza miastem.
- Gdy przedmiot, skrzynia albo drzwi znajdą się tuż obok postaci, pozostałe dźwięki milkną. Mod gra wtedy dźwięk możliwej interakcji i czyta nazwę.
- Przy otwartym ekwipunku dźwięki otoczenia milkną.

## Życie, mana i doświadczenie

- Z: procent życia.
- Shift + Z: procent many.
- X: procent doświadczenia brakujący do następnego poziomu.

## Karta postaci (C)

- Strzałka w górę i w dół: przechodzenie po polach. Pola to imię i klasa, poziom, doświadczenie, następny poziom, siła, zręczność, żywotność, energia, punkty do rozdania, punkty umiejętności, złoto, obrona, życie, mana, wytrzymałość i odporności.
- Enter na atrybucie (siła, zręczność, żywotność, energia): dodanie jednego punktu.
- Shift + Enter na atrybucie: dodanie wszystkich wolnych punktów.
- Spacja: ponowne odczytanie pola.
- Tab: przejście do ekwipunku, jeśli jest otwarty.

Gdy są wolne punkty, karta otwiera się od razu na sile.

## Ekwipunek (I)

Ekwipunek ma trzy obszary: założone przedmioty, plecak (10 kolumn na 4 wiersze) i pas.

- Strzałki: przechodzenie po polach. Z górnego wiersza plecaka strzałka w górę prowadzi do założonych przedmiotów, a z dolnego strzałka w dół do pasa.
- Enter: podniesienie przedmiotu na kursor, odłożenie go, założenie albo zamiana.
- Shift + Enter: użycie przedmiotu, na przykład wypicie mikstury, albo założenie broni lub zbroi prosto z plecaka.
- Spacja: pełny opis przedmiotu, taki sam jak w dymku gry. Zawiera obronę lub obrażenia, wytrzymałość, wymagania i właściwości magiczne. Działa w plecaku i na założonych przedmiotach, gdy nic nie trzymasz na kursorze.
- Tab: przejście do karty postaci, jeśli jest otwarta.

Mod potwierdza akcje: „Trzymasz:”, „Odłożono.”, „Założono.”, „Użyto:” albo „Nie można tego zrobić.”.

### Jak założyć przedmiot

Krótka droga: wybierz przedmiot w plecaku i naciśnij Shift + Enter. Gra sama włoży go na właściwe miejsce, a mod powie „Założono:” i nazwę. Pierścienie, amulety i drugą broń zakłada się dłuższą drogą, bo miejsce wybiera wtedy gra.

Dłuższa droga, w której sam wybierasz miejsce:

1. W plecaku wybierz przedmiot i naciśnij Enter. Usłyszysz „Trzymasz:” i nazwę.
2. Strzałką w górę przejdź z górnego wiersza plecaka do założonych przedmiotów.
3. Strzałkami w lewo i w prawo wybierz miejsce, na przykład „Prawa ręka” dla broni albo „Zbroja”.
4. Naciśnij Enter. Usłyszysz „Założono.”. Jeśli na tym miejscu coś już było, przedmioty się zamienią, a stary trafi na kursor („Trzymasz:”). Odłóż go Enterem na wolne pole plecaka.

Gdy postać nie spełnia wymagań przedmiotu, gra go nie założy i mod powie „Nie można tego zrobić.”.

## Drzewko umiejętności (T)

Drzewko działa jak drzewo w programach Windows. Na górnym poziomie są trzy zakładki klasy, na przykład u barbarzyńcy: umiejętności bojowe, mistrzostwa bojowe i okrzyki bojowe. Każda zakładka rozwija się do swoich dziesięciu umiejętności.

- Strzałka w górę i w dół: poprzedni lub następny widoczny element.
- Strzałka w prawo: rozwinięcie zakładki, a na rozwiniętej zakładce przejście do pierwszej umiejętności.
- Strzałka w lewo: z umiejętności powrót do zakładki, a na zakładce jej zwinięcie.
- Home i End: pierwszy i ostatni element.
- Enter na zakładce: rozwinięcie albo zwinięcie.
- Enter na umiejętności: dodanie jednego punktu umiejętności.
- Shift + Enter na umiejętności: dodanie tylu punktów, ile się da (najwyżej do 20).
- Spacja: opis umiejętności, wymagany poziom postaci, wymagane umiejętności oraz wiersz i kolumna w drzewku.

Po otwarciu drzewka mod mówi, ile jest punktów do wydania. Dla każdej umiejętności podaje:

- nazwę,
- poziom (i dodatkowe poziomy z przedmiotów) albo „nie nauczona”,
- stan:
  - „można dodać punkt”, gdy wszystkie wymagania są spełnione i masz wolne punkty,
  - „dostępna”, gdy wymagania są spełnione, ale brak wolnych punktów,
  - „zablokowana, wymaga: …” z listą brakujących wymagań,
  - „poziom maksymalny”.

### Jak działają umiejętności w Diablo II

Od początku gry w drzewku widać wszystkie 30 umiejętności klasy, ale punkt można dodać tylko wtedy, gdy:

- postać ma wymagany poziom: umiejętności są w wierszach dostępnych od poziomu 1, 6, 12, 18, 24 i 30,
- w każdą wymaganą wcześniejszą umiejętność wydano już choć jeden punkt.

Punkty umiejętności dostaje się za każdy nowy poziom postaci oraz za niektóre zadania. Po awansie otwórz drzewko (T): mod od razu powie, ile punktów czeka na wydanie.

## Menu gry (Escape)

Escape w trakcie gry otwiera menu z opcjami: opcje, zapisz i wyjdź z gry, powrót do gry.

- Strzałka w górę i w dół: wybór pozycji.
- Enter: zatwierdzenie, na przykład wejście do podmenu.
- Strzałka w lewo i w prawo: zmiana ustawienia albo przesunięcie suwaka.
- Escape: zamknięcie całego menu i powrót do gry. Do poprzedniego menu wraca pozycja „Poprzednie menu”.

Podmenu opcji zawiera opcje dźwięku, obrazu, automapy i sterowania. Mod czyta nazwę ustawienia, jego wartość (na przykład „włączone” czy „60%”) oraz pozycję na liście.

### Konfiguracja sterowania

Pozycja „Konfiguracja sterowania” otwiera listę wszystkich akcji gry razem z przypisanymi klawiszami.

- Strzałka w górę i w dół: wybór akcji. Mod czyta jej nazwę, klawisz główny i zapasowy oraz pozycję na liście. Nagłówki są pomijane.
- Strzałka w lewo i w prawo: przełączanie między klawiszem głównym a zapasowym.
- Enter: przypisanie nowego klawisza. Mod powie „Naciśnij nowy klawisz”; naciśnij wybrany klawisz albo Escape, aby anulować. Potem mod przeczyta akcję z nowym klawiszem.
- Tab: przejście do przycisków na dole (ustawienia domyślne, zatwierdzenie, anulowanie) i z powrotem do listy. Na przyciskach strzałki wybierają przycisk, a Enter go naciska.
- Spacja: ponowne przeczytanie wybranej akcji.

## Śmierć i zwłoki

- Gdy postać zginie, mod to powie. Założone przedmioty zostają przy zwłokach.
- W chwili śmierci gra sama zapisuje postać z zerowym życiem.
- Naciśnij Escape, aby odrodzić się w mieście z pełnym życiem. To życie trafi do zapisu dopiero przy następnym zapisie, dlatego wychodź z gry przez menu (Escape, „Zapisz i wyjdź z gry”), a nie zamykając okno.
- Zwłoki zostają tam, gdzie postać zginęła, także po wyjściu i ponownym wczytaniu gry. Jeśli zginęła w lochu, po ekwipunek trzeba tam wrócić — mod pokazuje cele tylko z lokacji, w której właśnie jesteś. Zwłoki są w kategorii „gracze” jako „Zwłoki:” i imię postaci. Wybierz je, podejdź (Shift + Home) i naciśnij E.
- Jeśli po wczytaniu postać ma zero życia, mod ostrzeże o tym zaraz po wejściu do gry. Porozmawiaj wtedy z uzdrowicielem w mieście (w pierwszym akcie z Akarą), a przywróci pełne życie.

## Dziennik zadań (Q)

Q otwiera i zamyka dziennik zadań. Mod czyta akt oraz wybrane zadanie razem z tekstem, który dziennik dla niego pokazuje, na przykład że zadanie czeka albo zostało wykonane.

- Strzałka w górę i w dół: poprzednie lub następne zadanie w akcie.
- Strzałka w lewo i w prawo: poprzedni lub następny akt. W aktach, do których postać jeszcze nie dotarła, nie ma zadań.
- Spacja: ponowne przeczytanie wybranego zadania.
- Q: zamknięcie dziennika.

## Rozmowy z NPC

Gdy otworzy się menu NPC, mod czyta tryb i zaznaczoną opcję.

- Strzałka w górę i w dół: poprzednia lub następna opcja. Gdy menu jest otwarte, strzałki nie przesuwają już postaci.
- Enter: wybór opcji.
- Escape: zamknięcie menu.

## Pliki moda

- `D2AccessSetup.exe` – asystent instalacji gry i moda oraz aktualizacji moda.
- `D2AccessLauncher.exe` – uruchamia grę z modem.
- `D2AccessHook.dll` – właściwy mod.
- `audio` – dźwięki pomocnicze.
- `D2Access.ini` – opcjonalne ustawienia (język).
- `D2AccessHook.log` i `D2AccessLauncher.log` – dzienniki, przydatne przy zgłaszaniu błędów.

## Dla twórców

Budowanie (Visual Studio 2022, Win32):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Podczas gry mod wystawia serwer MCP pod adresem `http://127.0.0.1:13450/mcp`. Pozwala on odpytywać grę (stan, cele, ścieżki, zdarzenia mowy) i naciskać klawisze, co ułatwia testy automatyczne. Adresy funkcji i struktur gry opisuje `docs/ida_notes.md`.
