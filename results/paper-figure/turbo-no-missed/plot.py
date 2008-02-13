from pylab import *
windows = ["0.1", "1", "3"]
names = ["crux", "crux.no", "sequest", "sequest.no"]
# names = ["crux"]
styles = ["k--o", "k-o", "m--s", "m-s"]
labels = ["Crux (w/ index)", "Crux (w/o index)", "Sequest (w/ index)", "Sequest (w/o) index"]

NUM_SPECTRA = 100.0
idx = 0
for name in names:
  ys = []
  xs = []
  for window in windows:
    filename = "%s.%s.time" % (window, name)
    fh = open(filename)
    for line in fh:
      if line.startswith("real"):
        secondsTime = float(line.split("m")[1].split("s")[0])
        minutesTime = float(line.split()[1].split("m")[0])
        ys.append( (secondsTime + 60.0 * minutesTime) / NUM_SPECTRA)
        xs.append(float(window))
    fh.close()

  loglog(xs, ys, styles[idx], label=labels[idx])
  idx += 1

legend(loc="lower right")
xlabel("Mass window (Da)", size=20)
xlim(0.05, 5)
ylim(0.001, 50.0)
xticks(size=20)
yticks(size=20)
ylabel("Runtime per spectrum (s)", size=20)
savefig("indexing-yeast-windows.eps")
savefig("indexing-yeast-windows.png")
      
