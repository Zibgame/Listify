# Listify 2

Application Windows C++17 / Qt 6 / FTXUI qui transforme un lien Etsy et un artwork Productfy en dossier de listing SEO complet.

## Pipeline

1. Reçoit manuellement un lien Etsy et un PNG, ou automatiquement un job créé par Productfy.
2. Extrait l'identifiant stable depuis `/listing/<id>`.
3. Interroge exclusivement Etsy Open API v3 via le proxy obligatoire pour récupérer la fiche et vérifier la boutique. Après la fiche principale, la boutique, l'inventaire et l'index des images sont demandés simultanément. Listify tente aussi de lire le catalogue réel de variantes Device/Style ; Etsy exige un jeton OAuth pour cet endpoint et le brief indique clairement « indisponible » si ce jeton manque.
4. Télécharge, toujours via le proxy, jusqu'aux trois premières images simultanément. L'ordre Etsy original est conservé dans le prompt et dans les fichiers.
5. Archive les données et images sources avant l'appel IA.
6. Envoie à OpenAI l'artwork Productfy, les images Etsy, les données de la fiche, le prompt du profil et les keywords facultatifs.
7. Valide strictement la sortie (titre 105–130 caractères, description d'au moins 500 mots, 13 tags, alt text 450–500 caractères). Une seconde tentative corrective est automatique si le premier résultat ne respecte pas le contrat.
8. Écrit les résultats dans `output/<listing_id>/`.

## Profils de niche

Chaque niche est un dossier sous `profiles/` :

```text
profiles/
  kindle_case/
    profile.json
    seo_generator.txt
    keyword.txt
    listing_style.txt
```

`keyword.txt` est facultatif dans le workflow. Le profil Kindle fourni est une copie des fichiers de `C:\Users\faniz\Documents\Kindle_Case` au moment de la compilation.

## Organisation d'un output

```text
output/<listing_id>/
  source/
    listing.json
    inventory.json
    images.json
    listing_01.jpg
    listing_02.jpg
    listing_03.jpg
    productfy_artwork.png
  input/
    seo_prompt.txt
    keywords.txt
    listing_style.txt
    product_brief.txt
  result/
    name.txt
    title.txt
    description.txt
    tags.txt
    alt_text.txt
    image_filename.txt
    listing.json
    listing.md
    chatgpt_output.txt
  debug/
    openai_response.json
  copy_results.cmd
  copy_results.ps1
```

Double-cliquez sur `copy_results.cmd` pour ouvrir un menu terminal permettant de copier chaque champ dans le presse-papiers. Dans le TUI, les touches `1` à `5` copient également les résultats.

## Configuration

Variables lues depuis `.env`, le projet voisin `Orderify`, puis le `.env` de Mockify :

- `ETSY_API_KEY` et éventuellement `ETSY_SECRET`
- `ETSY_ACCESS_TOKEN` facultatif pour les variantes protégées par OAuth
- `PROXY_HOST`, `PROXY_PORT`, `PROXY_USERNAME`, `PROXY_PASSWORD`
- `OPENAI_API_KEY`
- `OPENAI_SEO_MODEL` facultatif, valeur par défaut `gpt-6.2-astra`
- `OPENAI_REASONING_EFFORT` facultatif, valeur par défaut `high`
- `LISTIFY_SEO_PROMPT` et `LISTIFY_KEYWORDS` facultatifs pour remplacer les fichiers du profil

Pour le profil Kindle, Listify préfère automatiquement les fichiers originaux de `C:\Users\faniz\Documents\Kindle_Case` lorsqu'ils existent, et utilise les copies embarquées comme solution de repli. Les images sont légendées séparément dans le message envoyé au modèle et les données Etsy sont transformées en brief lisible, comme lors d'un envoi manuel dans ChatGPT.

Le fichier `result/generation_info.json` indique le modèle, l'effort, le nombre de tentatives, les tokens réellement consommés et une estimation du coût texte standard pour les modèles connus.

Sans proxy valide, Listify refuse toute connexion réseau directe.

## Compiler et tester sur ce PC

```powershell
cd C:\Users\faniz\Documents\Projects\Listify
& D:\Other\MSYS2\clang64\bin\cmake.exe -S . -B build -G Ninja `
  -DCMAKE_MAKE_PROGRAM=D:/Other/MSYS2/clang64/bin/ninja.exe `
  -DCMAKE_CXX_COMPILER=C:/Users/faniz/Documents/Projects/Productfy/.qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_PREFIX_PATH=C:/Users/faniz/Documents/Projects/Productfy/.qt/6.8.3/mingw_64 `
  -DCMAKE_BUILD_TYPE=Release
& D:\Other\MSYS2\clang64\bin\cmake.exe --build build --parallel
$env:Path = "C:\Users\faniz\Documents\Projects\Productfy\.qt\6.8.3\mingw_64\bin;C:\Users\faniz\Documents\Projects\Productfy\.qt\Tools\mingw1310_64\bin;$env:Path"
& D:\Other\MSYS2\clang64\bin\ctest.exe --test-dir build --output-on-failure
```

## Lancer

```powershell
.\Listify.exe
```

Ou directement avec des arguments :

```powershell
.\dist\Listify.exe --url "https://www.etsy.com/listing/123/example" --artwork "C:\chemin\artwork.png"
```

Productfy utilise `--job <fichier.json> --headless`. Dans ce mode, Listify n'ouvre aucun second TUI, écrit un statut JSON atomique et retourne `0` seulement lorsque tous les fichiers SEO sont validés.

Le lanceur racine doit rester à côté du dossier `dist`.
