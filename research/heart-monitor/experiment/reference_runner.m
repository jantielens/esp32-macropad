function reference_runner(input_path, output_path, fs)
    input = load(input_path);
    results = cell(columns(input.signals), 1);
    for index = 1:columns(input.signals)
        [msptd_peaks, msptd_onsets] = msptdfastv2_beat_detector(input.signals(:, index), fs);
        [qppg_peaks, qppg_onsets] = qppgfast_beat_detector(input.signals(:, index), fs);
        results{index} = struct('msptd_peaks', msptd_peaks, 'msptd_onsets', msptd_onsets, ...
                               'qppg_peaks', qppg_peaks, 'qppg_onsets', qppg_onsets);
    end
    save('-mat7-binary', output_path, 'results');
end