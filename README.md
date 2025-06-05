# Bachelor_Jannik_Hohmann

Hey, 
leider habe ich es nicht geschafft den Fehler der zu dem Hardware Interupt führt zu finden.
Anscheinend komme ich irgendwann nach einigen Iterationen beim Einfügen von Einträgen in die Liste vListInsert(lsit.c nach dem Builden in CCS) in einen Interupt.
Ich glaube es liegt entweder an einem Nullpointer der fälschlich in die Liste eingeführt wird oder daran, dass auf Speicher zugegriffen wird auf den nicht zugegriffen werden darf. 
Ich habe mit Brakepoints versucht heruaszufinden warum das passiert aber leider konnte ich nicht viel daraus lernen und bin jetzt ein bisschen Ratlos.
Was ich auch versucht habe ist die größe von Stack und Heap zu ändern (standard beide 8192, festgelegt in linker.cmd), aber leider hat das das problem auch nicht gelöst. 
Da ich in meiner Anpassung des Modells verusche sowohl den Surfacetype als auch eine Liste an den Warscheinlichkeiten für die Materialien zu übergeben, könnte ich mir vorstellen, dass der Fehler damit zusammenhängt. Leider fehlen mir jedoch die richtigen Lösungsansätze.
Ich habe auch ein wenig mit Compileroptimierung herumprobiert aber leider auch erfolglos. 
Ich suche die nächste Zeit nochmal nach anderen Lösungsansätzen.
Ich weiß zwar nicht wie viel du mir ohne das Board helfen kannst aber vielleicht fällt dir ja ein offensichtlicher Fehler auf.
Wenn du noch weitere Lösungsansätze oder Ideen hast wäre ich sehr dankbar dafür. Vielen Dank für deine Zeit. 
