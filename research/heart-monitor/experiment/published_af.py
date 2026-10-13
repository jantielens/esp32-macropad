"""Execute the pretrained PhysioNet nine-model vote; no retraining."""

import argparse
import json

from libsvm.svmutil import svm_load_model, svm_predict
import numpy as np
import pandas as pd
from scipy.io import loadmat, savemat
from scipy.special import expit
from sklearn.metrics import confusion_matrix

from references import octave
from run import OUTPUT

SELECT = np.array([2, 3, 4, 5, 6, 7, 12, 14]) - 1


def predict(feature_matrix, verify=False):
    originals = loadmat(OUTPUT / "af-models.mat", simplify_cells=True)["model_all"]
    means = loadmat(OUTPUT / "af-means.mat", simplify_cells=True)["meanx_all"]
    deviations = loadmat(OUTPUT / "af-stds.mat", simplify_cells=True)["stdx_all"]
    votes = []
    for index, (original, mean, deviation) in enumerate(zip(originals, means, deviations)):
        assert original["Parameters"][0] == 0 and original["Parameters"][1] == 2
        support = original["SVs"].toarray()
        coefficients = np.asarray(original["sv_coef"]).reshape(-1)
        lines = ["svm_type c_svc", "kernel_type rbf", f"gamma {original['Parameters'][3]:.17g}",
                 "nr_class 2", f"total_sv {original['totalSV']}", f"rho {original['rho']:.17g}",
                 "label " + " ".join(map(str, original["Label"])),
                 f"probA {original['ProbA']:.17g}", f"probB {original['ProbB']:.17g}",
                 "nr_sv " + " ".join(map(str, original["nSV"])), "SV"]
        lines.extend(f"{coefficient:.17g} " + " ".join(f"{column + 1}:{value:.17g}" for column, value in enumerate(row))
                     for coefficient, row in zip(coefficients, support))
        model_path = OUTPUT / f"af-converted-model-{index}.txt"
        model_path.write_text("\n".join(lines) + "\n")
        model = svm_load_model(str(model_path))
        normalized = (feature_matrix[:, SELECT] - np.asarray(mean)) / np.asarray(deviation)
        labels, _, _ = svm_predict([0] * len(normalized), normalized.tolist(), model, "-b 1 -q")
        if verify:
            squared = np.sum((normalized[:, None, :] - support[None, :, :]) ** 2, axis=2)
            decisions = np.exp(-original["Parameters"][3] * squared) @ coefficients - original["rho"]
            first_probability = expit(-(original["ProbA"] * decisions + original["ProbB"]))
            independent = np.where(first_probability >= 0.5, original["Label"][0], original["Label"][1])
            assert np.array_equal(labels, independent), f"Model conversion mismatch in fold {index}"
        votes.append(labels)
    votes = np.asarray(votes)
    return (np.sum(votes, axis=0) >= 5).astype(int), np.sum(votes, axis=0)


def main(validate):
    if validate:
        features = np.loadtxt(OUTPUT / "af-test_feature.txt", delimiter=",")[:100]
        predictions, _ = predict(features, verify=True)
        print("Nine pretrained model conversions verified on 100 supplied feature rows; AF votes", int(predictions.sum()))
        return
    windows = pd.read_csv(OUTPUT / "expanded-af-windows.csv")
    first_blocks = windows.groupby("subject").block.transform("min")
    selected = windows[windows.block == first_blocks].copy()
    cells = loadmat(OUTPUT / "expanded-af-intervals.mat")["intervals"]
    selected_cells = cells[selected.index.to_numpy(), :].copy()
    sample_rates = np.where(selected.method == "ecg-xqrs", 125, 100)
    for index, sample_rate in enumerate(sample_rates):
        selected_cells[index, 0] = np.rint(selected_cells[index, 0] * sample_rate / 1000)
    savemat(OUTPUT / "published-af-input.mat", {"intervals": selected_cells,
                                               "sample_rates": sample_rates[:, None]})
    octave(f"af_feature_runner('{OUTPUT}/published-af-input.mat', '{OUTPUT}/published-af-features.mat');")
    features = loadmat(OUTPUT / "published-af-features.mat")["features"]
    finite = np.isfinite(features[:, SELECT]).all(axis=1)
    selected.loc[:, "features_finite"] = finite
    selected.loc[:, "prediction_af"] = np.nan
    selected.loc[:, "af_model_votes"] = np.nan
    predictions, votes = predict(features[finite])
    selected.loc[finite, "prediction_af"] = predictions
    selected.loc[finite, "af_model_votes"] = votes
    selected.to_csv(OUTPUT / "published-af-results.csv", index=False)
    rows = []
    for method, frame in selected.groupby("method"):
        admitted = frame[frame.features_finite]
        true_negative, false_positive, false_negative, true_positive = confusion_matrix(
            admitted.label_af, admitted.prediction_af, labels=[0, 1]).ravel()
        rows.append({"method": method, "subjects": frame.subject.nunique(),
                     "evaluated_windows": len(admitted), "nonfinite_feature_windows": len(frame) - len(admitted),
                     "sensitivity": true_positive / (true_positive + false_negative),
                     "specificity": true_negative / (true_negative + false_positive),
                     "true_positive": int(true_positive), "false_negative": int(false_negative),
                     "false_positive": int(false_positive), "true_negative": int(true_negative)})
    summary = {"scope": "first complete two-minute block per admitted subject; three 30-second windows per method",
               "reference": "original PhysioNet AF_features and pretrained nine-model MIT-AFDB SVM vote, selected columns 2,3,4,5,6,7,12,14",
               "libsvm": "official libsvm 3.37, probability mode; parameters preserved from original model files",
               "interval_units": "native ECG sample counts at 125 Hz or PPG sample counts at 100 Hz; sampling rate passed to original entropy code",
               "no_training_on_evaluation_subjects": True,
               "limitations": ["ECG-trained model transferred to PPG intervals without recalibration",
                               "Automatic ECG proxies and subject-level AF labels",
                               "No validated motion SQI or separately labelled ectopy controls",
                               "AFEv implementation has patent notice; deployment requires review"],
               "results": rows}
    (OUTPUT / "published-af-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(pd.DataFrame(rows).to_string(index=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--validate", action="store_true")
    main(parser.parse_args().validate)