(() => {
    const emotions = ['content', 'relaxed', 'happy', 'worried', 'angry', 'sad', 'surprised', 'sleepy'];
    let fixedEmotion = 'content';
    const fail = (id, message) => {
        throw Object.assign(new Error(message), { fieldIds: [id] });
    };
    padRegisterWidgetEditor({
        type: 'face',
        available: info => !!(info && info.widget_catalog && info.widget_catalog.some(widget => widget.type === 'face')),
        sections: [{ id: 'pad-edit-face-section', tab: 'Content' }],
        fields: [
            { id: 'pad-edit-face-style', key: 'widget_face_style', kind: 'text', default: 'robot' },
            { id: 'pad-edit-face-emotion', key: 'widget_data_binding', kind: 'text', default: () => fixedEmotion, binding: {} },
            { id: 'pad-edit-face-liveliness', key: 'widget_face_liveliness', kind: 'text', default: 'normal' }
        ],
        actions: { tap: true, longPress: true },
        load: button => { fixedEmotion = button.widget_face_emotion || 'content'; },
        build: button => {
            if (!['robot', 'doodle'].includes(button.widget_face_style))
                fail('pad-edit-face-style', 'Choose Robot Eyes or Doodle Buddy.');
            if (!['still', 'calm', 'normal', 'lively'].includes(button.widget_face_liveliness))
                fail('pad-edit-face-liveliness', 'Choose Still, Calm, Normal, or Lively.');
            const value = button.widget_data_binding || 'content';
            if (value.length > 191)
                fail('pad-edit-face-emotion', 'Emotion source is too long (maximum 191 characters).');
            if (!emotions.includes(value) && !bindingTokenize(value).some(token => !token.unclosed))
                fail('pad-edit-face-emotion', 'Choose a supported emotion name or enter a binding.');
            button.widget_data_binding = value;
        }
    });
})();
