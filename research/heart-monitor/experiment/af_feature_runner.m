function af_feature_runner(input_path, output_path)
    input = load(input_path);
    features = nan(numel(input.intervals), 14);
    for index = 1:numel(input.intervals)
        try
            features(index, :) = AF_features(double(input.intervals{index}), double(input.sample_rates(index)));
        catch error
            fprintf(2, 'Feature row %d failed: %s\n', index, error.message);
        end
    end
    save('-mat7-binary', output_path, 'features');
end